#include "StdInclude.hpp"
#include "DemoParser.hpp"

#include <deque>

#include "Mod.hpp"
#include "Structures.hpp"
#include "Functions.hpp"
#include "Hooks/Playback.hpp"
#include "Hooks/Kills.hpp"
#include "Hooks/Diagnostics.hpp"

namespace IWXMVM::IW2::DemoParser
{
    using namespace Structures;

    constexpr uint32_t MAX_MSGLEN = 0x20000;
    constexpr uint32_t RECORD_HEADER_SIZE = 8;  // int32 serverMessageSequence + int32 length
    constexpr int ET_EVENTS = 10;
    constexpr int EV_OBITUARY = 198;
    constexpr int MAX_GENTITIES = 1024;
    constexpr size_t PLAYERSTATE_SIZE = 0x26A8;
    constexpr size_t FRAME_BACKUP = 32;

    int32_t demoStartTick = 0;
    int32_t demoEndTick = 0;
    uint32_t trailingBytes = RECORD_HEADER_SIZE;

    struct Record
    {
        uint32_t offset;  // file offset of the record header
        int32_t length;   // payload length
    };

    std::vector<Record> records;
    std::filesystem::path demoPath;

    // ---------------------------------------------------------------------------------------------------------
    // headless snapshot decoding state
    // ---------------------------------------------------------------------------------------------------------

    struct Frame
    {
        int32_t messageNum = 0;
        int32_t serverTime = 0;
        std::vector<entityState_t> entities;  // sorted by number
        std::vector<uint8_t> playerState;     // PLAYERSTATE_SIZE
    };

    struct ScanState
    {
        bool active = false;
        size_t nextRecord = 0;
        int32_t messageSequence = 0;  // serverMessageSequence of the record being decoded
        std::vector<entityState_t> baselines;
        std::deque<Frame> frames;  // most recent last
        std::array<int32_t, MAX_GENTITIES> previousEventSequence{};
        std::array<bool, MAX_GENTITIES> presentLastFrame{};
        std::vector<uint8_t> compressed;
        std::vector<uint8_t> decompressed;
        std::vector<uint8_t> scratchPlayerState;
        std::ifstream file;
        size_t killsFound = 0;
        size_t snapshotsDecoded = 0;
        uint32_t vidRestartsAtStart = 0;
        uint32_t comErrorsAtStart = 0;
        size_t gamestatesDecoded = 0;
    } scan;

    std::pair<int32_t, int32_t> GetDemoTickRange()
    {
        return {demoStartTick, demoEndTick};
    }

    uint32_t GetTrailingBytes()
    {
        return trailingBytes;
    }

    bool IsScanning()
    {
        return scan.active;
    }

    void ResetScan();

    void CancelScan()
    {
        if (scan.active)
            LOG_DEBUG("Event scan skipped (complete cache available)");
        ResetScan();
    }

    float GetScanProgress()
    {
        if (records.empty())
            return 0.0f;
        return static_cast<float>(scan.nextRecord) / static_cast<float>(records.size());
    }

    void ResetScan()
    {
        scan.active = false;
        scan.nextRecord = 0;
        scan.baselines.clear();
        scan.frames.clear();
        scan.previousEventSequence.fill(0);
        scan.presentLastFrame.fill(false);
        if (scan.file.is_open())
            scan.file.close();
        scan.killsFound = 0;
        scan.snapshotsDecoded = 0;
    }

    void Reset()
    {
        demoStartTick = 0;
        demoEndTick = 0;
        trailingBytes = RECORD_HEADER_SIZE;
        records.clear();
        ResetScan();
    }

    // ---------------------------------------------------------------------------------------------------------
    // record access
    // ---------------------------------------------------------------------------------------------------------

    bool ReadPayload(std::ifstream& file, const Record& record, std::vector<uint8_t>& compressed)
    {
        compressed.resize(record.length);
        file.clear();
        file.seekg(record.offset + RECORD_HEADER_SIZE, std::ios::beg);
        file.read(reinterpret_cast<char*>(compressed.data()), record.length);
        return static_cast<bool>(file);
    }

    // Decompresses a record payload into 'decompressed' and sets up a msg_t over it. The first 4 raw bytes
    // (reliableAcknowledge) are skipped. Returns false if the payload is unusable.
    bool OpenMessage(const std::vector<uint8_t>& compressed, std::vector<uint8_t>& decompressed, msg_t& msg)
    {
        if (compressed.size() <= 4)
            return false;

        decompressed.resize(MAX_MSGLEN + 16);
        const auto size = Functions::MSG_ReadBitsCompress(compressed.data() + 4, decompressed.data(),
                                                          static_cast<int>(compressed.size() - 4));
        if (size <= 0 || static_cast<size_t>(size) > MAX_MSGLEN)
            return false;

        msg = {};
        msg.data = decompressed.data();
        msg.maxsize = static_cast<int>(decompressed.size());
        msg.cursize = size;
        msg.readcount = 0;
        msg.bit = 0;
        return true;
    }

    // Cheap probe used for the demo bounds: only looks for the first svc_snapshot server time in a message,
    // skipping server commands; stops at anything that needs bit-level decoding.
    std::optional<int32_t> ProbeSnapshotTime(msg_t& msg, bool& isGamestate)
    {
        isGamestate = false;
        while (msg.readcount < msg.cursize && !msg.overflowed)
        {
            const auto cmd = Functions::MSG_ReadByte(&msg);
            switch (cmd)
            {
                case svc_nop:
                    continue;
                case svc_serverCommand:
                    Functions::MSG_ReadLong(&msg);
                    Functions::MSG_ReadString(&msg);
                    continue;
                case svc_snapshot:
                    return Functions::MSG_ReadLong(&msg);
                case svc_gamestate:
                    isGamestate = true;
                    return std::nullopt;
                default:
                    return std::nullopt;
            }
        }
        return std::nullopt;
    }

    // ---------------------------------------------------------------------------------------------------------
    // Run: index + bounds, then arm the event scan
    // ---------------------------------------------------------------------------------------------------------

    void Run()
    {
        Reset();

        demoPath = Hooks::Playback::GetCurrentDemoPath();
        std::ifstream file(demoPath, std::ios::binary);
        if (!file.is_open())
        {
            throw std::runtime_error("failed to open demo file " + demoPath.string());
        }

        file.seekg(0, std::ios::end);
        const auto fileSize = static_cast<uint32_t>(file.tellg());
        file.seekg(0, std::ios::beg);

        // Pass 1: index all records
        uint32_t endOfLastCompleteRecord = 0;
        uint32_t offset = 0;

        while (offset + RECORD_HEADER_SIZE <= fileSize)
        {
            int32_t header[2];
            file.seekg(offset, std::ios::beg);
            file.read(reinterpret_cast<char*>(header), sizeof(header));
            if (!file)
                break;

            const auto length = header[1];
            if (length == -1)
                break;  // proper end-of-demo marker

            if (length < 0 || static_cast<uint32_t>(length) > MAX_MSGLEN ||
                offset + RECORD_HEADER_SIZE + static_cast<uint32_t>(length) > fileSize)
            {
                LOG_WARN("Demo record at offset {} has invalid length {}; treating as truncated", offset, length);
                break;
            }

            records.push_back({offset, length});
            offset += RECORD_HEADER_SIZE + static_cast<uint32_t>(length);
            endOfLastCompleteRecord = offset;
        }

        trailingBytes = fileSize - endOfLastCompleteRecord;

        if (records.empty())
        {
            LOG_ERROR("Demo contains no complete records. Cannot determine demo length.");
            return;
        }

        LOG_DEBUG("Indexed {} demo records, {} trailing bytes", records.size(), trailingBytes);

        // Pass 2: probe a handful of records at both ends for the first / last snapshot time
        Functions::EnsureHuffmanInitialized();

        std::vector<uint8_t> compressed;
        std::vector<uint8_t> decompressed;
        msg_t msg{};

        std::optional<int32_t> firstSnapshotTime;
        std::optional<int32_t> lastSnapshotTime;
        constexpr size_t PROBE_COUNT = 16;

        for (size_t i = 0; i < records.size() && i < PROBE_COUNT && !firstSnapshotTime.has_value(); ++i)
        {
            if (!ReadPayload(file, records[i], compressed) || !OpenMessage(compressed, decompressed, msg))
                break;

            bool isGamestate = false;
            const auto time = ProbeSnapshotTime(msg, isGamestate);
            if (i == 0)
            {
                LOG_DEBUG("First demo record is {} ({} bytes)", isGamestate ? "a gamestate" : "NOT a gamestate",
                          records[i].length);
            }
            if (time.has_value() && time.value() > 0)
                firstSnapshotTime = time;
        }

        for (size_t n = 0; n < records.size() && n < PROBE_COUNT && !lastSnapshotTime.has_value(); ++n)
        {
            const auto& record = records[records.size() - 1 - n];
            if (!ReadPayload(file, record, compressed) || !OpenMessage(compressed, decompressed, msg))
                break;

            bool isGamestate = false;
            const auto time = ProbeSnapshotTime(msg, isGamestate);
            if (time.has_value() && time.value() > 0)
                lastSnapshotTime = time;
        }

        if (!firstSnapshotTime.has_value() || !lastSnapshotTime.has_value() ||
            lastSnapshotTime.value() <= firstSnapshotTime.value())
        {
            LOG_ERROR("Could not determine demo bounds (first: {}, last: {})", firstSnapshotTime.value_or(-1),
                      lastSnapshotTime.value_or(-1));
            return;
        }

        demoStartTick = firstSnapshotTime.value();
        demoEndTick = lastSnapshotTime.value() + 500;

        LOG_DEBUG("Determined demo bounds as {} and {} ({} records)", demoStartTick, demoEndTick, records.size());

        if (demoEndTick - demoStartTick > 3600 * 1000 * 3 || demoEndTick - demoStartTick < 1000)
        {
            LOG_ERROR("Demo bounds look invalid ({} ms). Cannot render timeline.", demoEndTick - demoStartTick);
            demoStartTick = demoEndTick = 0;
            return;
        }

        // Pass 3 (incremental, see Step): decode every snapshot for events
        scan.file.open(demoPath, std::ios::binary);
        if (!scan.file.is_open())
        {
            LOG_WARN("Could not open the demo for event scanning");
            return;
        }

        scan.baselines.assign(MAX_GENTITIES, entityState_t{});
        scan.scratchPlayerState.assign(PLAYERSTATE_SIZE, 0);
        scan.vidRestartsAtStart = Hooks::Diagnostics::GetVidRestartCount();
        scan.comErrorsAtStart = Hooks::Diagnostics::GetComErrorCount();
        scan.active = true;
        LOG_DEBUG("Event scan armed ({} records)", records.size());
    }

    // ---------------------------------------------------------------------------------------------------------
    // event scan
    // ---------------------------------------------------------------------------------------------------------

    void ResetWorld()
    {
        std::fill(scan.baselines.begin(), scan.baselines.end(), entityState_t{});
        scan.frames.clear();
        scan.previousEventSequence.fill(0);
        scan.presentLastFrame.fill(false);
    }

    void ParseGamestate(msg_t& msg)
    {
        ResetWorld();
        ++scan.gamestatesDecoded;

        Functions::MSG_ReadLong(&msg);  // serverCommandSequence

        static entityState_t nullState{};
        while (!msg.overflowed && msg.readcount < msg.cursize)
        {
            const auto cmd = Functions::MSG_ReadByte(&msg);
            if (cmd == svc_EOF || cmd < 0)
                break;

            if (cmd == 2)  // svc_configstring: short index, big string
            {
                Functions::MSG_ReadBits(&msg, 16);
                Functions::MSG_ReadBigString(&msg);
            }
            else if (cmd == 3)  // svc_baseline
            {
                const auto number = Functions::MSG_ReadBits(&msg, 10);
                if (number < 0 || number >= MAX_GENTITIES)
                    break;
                entityState_t state{};
                Functions::MSG_ReadDeltaEntity(&msg, &nullState, &state, number);
                scan.baselines[number] = state;
            }
            else
            {
                break;  // unknown: give up on this message
            }
        }
        // clientNum + checksumFeed follow; nothing else of interest in this message
    }

    void CheckEntityEvents(const entityState_t& state, bool wasPresent, int32_t serverTime)
    {
        const auto number = state.number;
        if (number < 0 || number >= MAX_GENTITIES)
            return;

        if (state.eType > ET_EVENTS)
        {
            // temp entity carrying a single event; the game fires it once, when the entity first shows up
            if (!wasPresent && state.eType - ET_EVENTS == EV_OBITUARY)
            {
                Hooks::Kills::AddKill(serverTime, state.attackerEntityNum, state.otherEntityNum);
                ++scan.killsFound;
            }
            return;
        }

        // regular entity: events are queued in events[4], indexed by eventSequence (mirrors CG_CheckEvents)
        auto& previous = scan.previousEventSequence[number];
        if (!wasPresent)
            previous = 0;

        const auto sequence = state.eventSequence;
        if (sequence == 0)
        {
            previous = 0;
            return;
        }

        if (sequence < previous)
            previous -= 256;
        if (sequence - previous > 4)
            previous = sequence - 4;

        for (auto i = previous; i < sequence; ++i)
        {
            if (state.events[i & 3] == EV_OBITUARY)
            {
                Hooks::Kills::AddKill(serverTime, state.attackerEntityNum, state.otherEntityNum);
                ++scan.killsFound;
            }
        }
        previous = sequence;
    }

    void ParseSnapshot(msg_t& msg)
    {
        Frame frame;
        frame.serverTime = Functions::MSG_ReadLong(&msg);
        frame.messageNum = scan.messageSequence;
        const auto deltaByte = Functions::MSG_ReadByte(&msg);
        Functions::MSG_ReadByte(&msg);  // snapFlags

        const Frame* oldFrame = nullptr;
        if (deltaByte > 0)
        {
            const auto deltaNum = frame.messageNum - deltaByte;
            for (const auto& candidate : scan.frames)
            {
                if (candidate.messageNum == deltaNum)
                {
                    oldFrame = &candidate;
                    break;
                }
            }
            // A delta against a frame we don't have (dropped before the demo started, or an older gamestate):
            // decoding still works bit-wise, the unchanged fields just come out as zero.
        }

        // player state (values irrelevant here, but it has to be consumed bit-exactly)
        frame.playerState.assign(PLAYERSTATE_SIZE, 0);
        Functions::MSG_ReadDeltaPlayerstate(&msg, oldFrame ? oldFrame->playerState.data() : nullptr,
                                            frame.playerState.data());

        // packet entities (mirrors CL_ParsePacketEntities)
        std::array<bool, MAX_GENTITIES> presentNow{};
        size_t oldIndex = 0;
        const auto OldNumber = [&]() -> int {
            return (oldFrame && oldIndex < oldFrame->entities.size()) ? oldFrame->entities[oldIndex].number : 99999;
        };

        while (!msg.overflowed)
        {
            const auto newNumber = Functions::MSG_ReadBits(&msg, 10);
            if (newNumber == 1023 || newNumber < 0)
                break;
            if (msg.readcount > msg.cursize)
                break;

            // unchanged entities from the old frame
            while (OldNumber() < newNumber)
            {
                frame.entities.push_back(oldFrame->entities[oldIndex]);
                presentNow[oldFrame->entities[oldIndex].number] = true;
                ++oldIndex;
            }

            entityState_t state{};
            bool removed;
            if (OldNumber() == newNumber)
            {
                removed = Functions::MSG_ReadDeltaEntity(&msg, &oldFrame->entities[oldIndex], &state, newNumber);
                ++oldIndex;
            }
            else
            {
                removed = Functions::MSG_ReadDeltaEntity(&msg, &scan.baselines[newNumber & (MAX_GENTITIES - 1)],
                                                         &state, newNumber);
            }

            if (!removed)
            {
                state.number = newNumber;
                frame.entities.push_back(state);
                presentNow[newNumber] = true;
                CheckEntityEvents(state, scan.presentLastFrame[newNumber], frame.serverTime);
            }
        }

        // remaining unchanged entities
        while (OldNumber() != 99999)
        {
            frame.entities.push_back(oldFrame->entities[oldIndex]);
            presentNow[oldFrame->entities[oldIndex].number] = true;
            ++oldIndex;
        }

        scan.presentLastFrame = presentNow;
        scan.frames.push_back(std::move(frame));
        while (scan.frames.size() > FRAME_BACKUP)
            scan.frames.pop_front();
        ++scan.snapshotsDecoded;
    }

    void DecodeRecord(const Record& record)
    {
        int32_t header[2];
        scan.file.clear();
        scan.file.seekg(record.offset, std::ios::beg);
        scan.file.read(reinterpret_cast<char*>(header), sizeof(header));
        scan.messageSequence = header[0];

        msg_t msg{};
        if (!ReadPayload(scan.file, record, scan.compressed) || !OpenMessage(scan.compressed, scan.decompressed, msg))
            return;

        while (msg.readcount < msg.cursize && !msg.overflowed)
        {
            const auto cmd = Functions::MSG_ReadByte(&msg);
            switch (cmd)
            {
                case svc_nop:
                    continue;
                case svc_serverCommand:
                    Functions::MSG_ReadLong(&msg);
                    Functions::MSG_ReadString(&msg);
                    continue;
                case svc_gamestate:
                    ParseGamestate(msg);
                    return;
                case svc_snapshot:
                    ParseSnapshot(msg);
                    return;  // the snapshot is the last thing in a message
                default:
                    return;  // svc_download / EOF / garbage
            }
        }
    }

    bool Step(double budgetMs)
    {
        if (!scan.active)
            return false;

        if (Hooks::Diagnostics::GetVidRestartCount() != scan.vidRestartsAtStart ||
            Hooks::Diagnostics::GetComErrorCount() != scan.comErrorsAtStart)
        {
            // a Com_Error inside one of the game's decoders longjmps right out of the scan; never continue after that
            LOG_ERROR("Event scan cancelled at record {} of {}: the game raised an error / restarted the renderer",
                      scan.nextRecord, records.size());
            ResetScan();
            return false;
        }

        const auto start = std::chrono::steady_clock::now();
        try
        {
            while (scan.nextRecord < records.size())
            {
                DecodeRecord(records[scan.nextRecord]);
                ++scan.nextRecord;

                if (scan.nextRecord == 1 || scan.nextRecord == 2 || scan.nextRecord == 100 || scan.nextRecord % 10000 == 0)
                {
                    LOG_DEBUG("Event scan: {} / {} records, {} gamestates, {} snapshots, {} kills", scan.nextRecord,
                              records.size(), scan.gamestatesDecoded, scan.snapshotsDecoded, scan.killsFound);
                }

                const auto elapsed = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start);
                if (elapsed.count() >= budgetMs)
                    break;
            }
        }
        catch (...)
        {
            LOG_ERROR("Event scan aborted at record {} of {} (exception while decoding)", scan.nextRecord, records.size());
            scan.nextRecord = records.size();
        }

        if (scan.nextRecord >= records.size())
        {
            LOG_INFO("Event scan finished: {} snapshots decoded, {} kills found", scan.snapshotsDecoded, scan.killsFound);
            ResetScan();
            Hooks::Kills::OnScanFinished();
            return false;
        }

        return true;
    }
}  // namespace IWXMVM::IW2::DemoParser
