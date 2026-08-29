#include "StdInclude.hpp"
#include "Fog.hpp"

#include <cctype>

#include "Utilities/HookManager.hpp"
#include "../Addresses.hpp"
#include "../Structures.hpp"

namespace IWXMVM::IW2::Hooks::Fog
{
    namespace
    {
        constexpr int CS_FOGVARS = 12;

        // The exponential script fog of every stock MP map, exactly as its map GSC passes it to
        // setExpFog (density, r, g, b). mp_decoy sets no fog and is deliberately absent.
        struct MapFog
        {
            const char* map;
            float density;
            float r, g, b;
        };

        constexpr MapFog STOCK_MAP_FOG[] = {
            {"mp_breakout", 0.00015f, 0.15f, 0.14f, 0.13f},
            {"mp_brecourt", 0.0001f, 0.30f, 0.31f, 0.34f},
            {"mp_burgundy", 0.00015f, 0.7f, 0.85f, 1.0f},
            {"mp_carentan", 0.0001f, 0.55f, 0.6f, 0.55f},
            {"mp_dawnville", 0.00025f, 0.32f, 0.36f, 0.40f},
            {"mp_downtown", 0.00028f, 0.58f, 0.57f, 0.57f},
            {"mp_farmhouse", 0.00015f, 0.15f, 0.14f, 0.13f},
            {"mp_harbor", 0.00028f, 0.58f, 0.57f, 0.57f},
            {"mp_leningrad", 0.00045f, 0.58f, 0.57f, 0.57f},
            {"mp_matmata", 0.0002f, 0.5f, 0.5f, 0.5f},
            {"mp_railyard", 0.00015f, 0.8f, 0.8f, 0.8f},
            {"mp_rhine", 0.0001f, 0.55f, 0.6f, 0.55f},
            {"mp_toujane", 0.00015f, 0.9f, 0.95f, 1.0f},
            {"mp_trainstation", 0.000125f, 0.7f, 0.85f, 1.0f},
        };

        // A stock map's ambient-weather emitters: the `scr_allow_ambient_weather` loopfx blocks of
        // the map GSCs (blowing dust, fog banks, snow, smoke banks). Comp mods suppress these along
        // with the fog, and since they are server-spawned looped-fx entities they are simply absent
        // from such demos - so restoring the fog also replays them client-side. The effect assets
        // themselves are always precached (the GSCs' loadfx calls run unconditionally); only the
        // emitter entities are missing.
        struct FxEmitter
        {
            const char* efxPath;  // as passed to loadfx = the fx-name configstring content, verbatim
            float origin[3];
            float delayMs;
            float target[3];  // the point the effect is aimed at (loopfx's second position)
        };

        struct MapAmbient
        {
            const char* map;
            const FxEmitter* emitters;
            size_t count;
        };

        constexpr FxEmitter AMBIENT_MP_BREAKOUT[] = {
            {"fx/smoke/battlefield_smokebank_S.efx", {4240.f, 5284.f, 37.f}, 1000.f, {4240.f, 5284.f, 137.f}},
            {"fx/smoke/battlefield_smokebank_S.efx", {4758.f, 4628.f, 9.f}, 1000.f, {4758.f, 4628.f, 109.f}},
            {"fx/smoke/battlefield_smokebank_S.efx", {6173.f, 5346.f, 16.f}, 1000.f, {6173.f, 5346.f, 116.f}},
            {"fx/smoke/battlefield_smokebank_S.efx", {6983.f, 5362.f, 11.f}, 1000.f, {6983.f, 5362.f, 111.f}},
            {"fx/smoke/battlefield_smokebank_S.efx", {6021.f, 4506.f, -21.f}, 1000.f, {6021.f, 4506.f, 78.f}},
            {"fx/smoke/battlefield_smokebank_S.efx", {5329.f, 3309.f, 25.f}, 1000.f, {5329.f, 3309.f, 125.f}},
            {"fx/smoke/battlefield_smokebank_S.efx", {4250.f, 3261.f, -10.f}, 1000.f, {4250.f, 3261.f, 89.f}},
            {"fx/smoke/battlefield_smokebank_S.efx", {3297.f, 3336.f, 9.f}, 1000.f, {3297.f, 3336.f, 109.f}},
            {"fx/smoke/battlefield_smokebank_S.efx", {3182.f, 4365.f, -3.f}, 1000.f, {3182.f, 4365.f, 96.f}},
            {"fx/smoke/battlefield_smokebank_S.efx", {3190.f, 5324.f, -31.f}, 1000.f, {3190.f, 5324.f, 68.f}},
            {"fx/smoke/battlefield_smokebank_S.efx", {5697.f, 6475.f, -45.f}, 1000.f, {5697.f, 6475.f, 53.f}},
            {"fx/smoke/battlefield_smokebank_S.efx", {4792.f, 5770.f, 24.f}, 1000.f, {4792.f, 5770.f, 123.f}},
            {"fx/smoke/battlefield_smokebank_S.efx", {6591.f, 6710.f, -66.f}, 1000.f, {6591.f, 6710.f, 33.f}},
            {"fx/smoke/battlefield_smokebank_S.efx", {6701.f, 6071.f, -34.f}, 1000.f, {6701.f, 6071.f, 64.f}},
            {"fx/smoke/battlefield_smokebank_S.efx", {5280.f, 5039.f, -13.f}, 1000.f, {5280.f, 5039.f, 86.f}},
        };
        constexpr FxEmitter AMBIENT_MP_BRECOURT[] = {
            {"fx/smoke/thin_light_smoke_L.efx", {1817.f, -1474.f, -10.f}, 1000.f, {1817.f, -1474.f, 89.f}},
            {"fx/smoke/thin_light_smoke_L.efx", {2365.f, -1832.f, -14.f}, 1000.f, {2365.f, -1832.f, 85.f}},
            {"fx/smoke/thin_light_smoke_L.efx", {2860.f, -2869.f, -22.f}, 1000.f, {2860.f, -2869.f, 77.f}},
            {"fx/smoke/thin_light_smoke_L.efx", {315.f, -2372.f, 10.f}, 1000.f, {315.f, -2372.f, 110.f}},
            {"fx/smoke/thin_light_smoke_L.efx", {-2150.f, -1766.f, -15.f}, 1000.f, {-2150.f, -1766.f, 84.f}},
            {"fx/misc/fogbank_small_duhoc.efx", {3350.f, -1762.f, -49.f}, 2000.f, {3350.f, -1762.f, 49.f}},
            {"fx/misc/fogbank_small_duhoc.efx", {3650.f, -188.f, -135.f}, 2000.f, {3650.f, -188.f, -36.f}},
            {"fx/misc/fogbank_small_duhoc.efx", {3520.f, 712.f, -138.f}, 2000.f, {3520.f, 712.f, -39.f}},
            {"fx/misc/fogbank_small_duhoc.efx", {3803.f, -903.f, -74.f}, 2000.f, {3803.f, -903.f, 24.f}},
            {"fx/misc/fogbank_small_duhoc.efx", {2578.f, 436.f, -126.f}, 2000.f, {2578.f, 436.f, -27.f}},
            {"fx/misc/fogbank_small_duhoc.efx", {1567.f, 833.f, -126.f}, 2000.f, {1567.f, 833.f, -27.f}},
            {"fx/misc/fogbank_small_duhoc.efx", {1409.f, 1651.f, -114.f}, 2000.f, {1409.f, 1651.f, -15.f}},
            {"fx/misc/fogbank_small_duhoc.efx", {2686.f, 1385.f, -168.f}, 2000.f, {2686.f, 1385.f, -69.f}},
            {"fx/misc/fogbank_small_duhoc.efx", {476.f, 1542.f, -121.f}, 2000.f, {476.f, 1542.f, -22.f}},
            {"fx/misc/fogbank_small_duhoc.efx", {-622.f, 1872.f, -104.f}, 2000.f, {-622.f, 1872.f, -5.f}},
            {"fx/misc/fogbank_small_duhoc.efx", {-68.f, 1007.f, -126.f}, 2000.f, {-68.f, 1007.f, -27.f}},
            {"fx/misc/fogbank_small_duhoc.efx", {-1169.f, 588.f, -77.f}, 2000.f, {-1169.f, 588.f, 21.f}},
            {"fx/misc/fogbank_small_duhoc.efx", {-1658.f, 1516.f, -37.f}, 2000.f, {-1658.f, 1516.f, 61.f}},
            {"fx/misc/fogbank_small_duhoc.efx", {-1949.f, 570.f, -89.f}, 2000.f, {-1949.f, 570.f, 9.f}},
            {"fx/misc/fogbank_small_duhoc.efx", {-2855.f, 1406.f, 23.f}, 2000.f, {-2855.f, 1406.f, 122.f}},
            {"fx/misc/fogbank_small_duhoc.efx", {-2265.f, -706.f, -25.f}, 2000.f, {-2265.f, -706.f, 73.f}},
            {"fx/misc/fogbank_small_duhoc.efx", {-1569.f, -1941.f, -35.f}, 2000.f, {-1569.f, -1941.f, 63.f}},
            {"fx/misc/fogbank_small_duhoc.efx", {-301.f, -1613.f, -23.f}, 2000.f, {-301.f, -1613.f, 75.f}},
            {"fx/misc/fogbank_small_duhoc.efx", {-56.f, -3279.f, 6.f}, 2000.f, {-56.f, -3279.f, 105.f}},
            {"fx/misc/fogbank_small_duhoc.efx", {-1327.f, -732.f, -7.f}, 2000.f, {-1327.f, -732.f, 91.f}},
            {"fx/misc/fogbank_small_duhoc.efx", {215.f, -811.f, -7.f}, 2000.f, {215.f, -811.f, 91.f}},
            {"fx/misc/fogbank_small_duhoc.efx", {1793.f, -861.f, -7.f}, 2000.f, {1793.f, -861.f, 91.f}},
            {"fx/misc/fogbank_small_duhoc.efx", {2892.f, -1010.f, -7.f}, 2000.f, {2892.f, -1010.f, 91.f}},
            {"fx/misc/fogbank_small_duhoc.efx", {803.f, -1855.f, -49.f}, 2000.f, {803.f, -1855.f, 49.f}},
            {"fx/misc/fogbank_small_duhoc.efx", {1765.f, -2999.f, 8.f}, 2000.f, {1765.f, -2999.f, 107.f}},
            {"fx/misc/fogbank_small_duhoc.efx", {1197.f, -2393.f, 5.f}, 2000.f, {1197.f, -2393.f, 104.f}},
        };
        constexpr FxEmitter AMBIENT_MP_BURGUNDY[] = {
            {"fx/misc/fogbank_small_duhoc.efx", {235.f, 1406.f, 10.f}, 2000.f, {235.f, 1406.f, 20.f}},
            {"fx/misc/fogbank_small_duhoc.efx", {31.f, 877.f, 0.f}, 2000.f, {31.f, 877.f, 10.f}},
            {"fx/misc/fogbank_small_duhoc.efx", {1674.f, 2014.f, -39.f}, 2000.f, {1674.f, 2014.f, -29.f}},
            {"fx/misc/fogbank_small_duhoc.efx", {1571.f, 359.f, 4.f}, 2000.f, {1571.f, 359.f, 14.f}},
            {"fx/dust/dust_wind_eldaba.efx", {769.f, -189.f, 40.f}, 1000.f, {769.f, -189.f, 50.f}},
            {"fx/dust/dust_wind_eldaba.efx", {941.f, 351.f, 40.f}, 1000.f, {941.f, 351.f, 50.f}},
            {"fx/dust/dust_wind_eldaba.efx", {286.f, -26.f, 8.f}, 1000.f, {286.f, -26.f, 18.f}},
            {"fx/dust/dust_wind_eldaba.efx", {-1154.f, 1517.f, 19.f}, 1000.f, {-1154.f, 1517.f, 29.f}},
            {"fx/dust/dust_wind_eldaba.efx", {-1060.f, 797.f, 19.f}, 1000.f, {-1060.f, 797.f, 29.f}},
            {"fx/dust/dust_wind_eldaba.efx", {-1071.f, 2040.f, 19.f}, 1000.f, {-1071.f, 2040.f, 29.f}},
            {"fx/dust/dust_wind_eldaba.efx", {-402.f, 2658.f, 4.f}, 1000.f, {-402.f, 2658.f, 14.f}},
            {"fx/dust/dust_wind_eldaba.efx", {725.f, 3334.f, 16.f}, 1000.f, {725.f, 3334.f, 26.f}},
            {"fx/dust/dust_wind_eldaba.efx", {803.f, 2819.f, 16.f}, 1000.f, {803.f, 2819.f, 26.f}},
            {"fx/dust/dust_wind_eldaba.efx", {667.f, 2331.f, 16.f}, 1000.f, {667.f, 2331.f, 26.f}},
            {"fx/dust/dust_wind_eldaba.efx", {646.f, 1728.f, 16.f}, 1000.f, {646.f, 1728.f, 26.f}},
            {"fx/dust/dust_wind_eldaba.efx", {777.f, 1292.f, 16.f}, 1000.f, {777.f, 1292.f, 26.f}},
            {"fx/dust/dust_wind_eldaba.efx", {605.f, 752.f, 16.f}, 1000.f, {605.f, 752.f, 26.f}},
            {"fx/dust/dust_wind_eldaba.efx", {1520.f, 1368.f, 14.f}, 1000.f, {1520.f, 1368.f, 24.f}},
        };
        constexpr FxEmitter AMBIENT_MP_CARENTAN[] = {
            {"fx/misc/fogbank_small_duhoc.efx", {1568.f, 2712.f, -47.f}, 2000.f, {1568.f, 2712.f, 52.f}},
            {"fx/misc/fogbank_small_duhoc.efx", {-1102.f, 1725.f, -27.f}, 2000.f, {-1102.f, 1725.f, 72.f}},
            {"fx/misc/fogbank_small_duhoc.efx", {567.f, 3433.f, -52.f}, 2000.f, {567.f, 3433.f, 47.f}},
            {"fx/dust/dust_wind_eldaba.efx", {-240.f, 1574.f, -23.f}, 1000.f, {-240.f, 1574.f, 76.f}},
            {"fx/dust/dust_wind_eldaba.efx", {-240.f, 1110.f, -23.f}, 1000.f, {-240.f, 1110.f, 76.f}},
            {"fx/dust/dust_wind_eldaba.efx", {473.f, 966.f, -2.f}, 1000.f, {473.f, 966.f, 97.f}},
            {"fx/dust/dust_wind_eldaba.efx", {1546.f, 2003.f, -42.f}, 1000.f, {1546.f, 2003.f, 57.f}},
            {"fx/dust/dust_wind_eldaba.efx", {1511.f, 1078.f, -39.f}, 1000.f, {1511.f, 1078.f, 60.f}},
            {"fx/dust/dust_wind_eldaba.efx", {1398.f, 349.f, -16.f}, 1000.f, {1398.f, 349.f, 83.f}},
            {"fx/dust/dust_wind_eldaba.efx", {935.f, 2569.f, -49.f}, 1000.f, {935.f, 2569.f, 50.f}},
            {"fx/dust/dust_wind_eldaba.efx", {-228.f, 2353.f, -15.f}, 1000.f, {-228.f, 2353.f, 84.f}},
            {"fx/dust/dust_wind_eldaba.efx", {470.f, 238.f, 2.f}, 1000.f, {470.f, 238.f, 101.f}},
            {"fx/dust/dust_wind_eldaba.efx", {362.f, -202.f, 46.f}, 1000.f, {362.f, -202.f, 145.f}},
            {"fx/dust/dust_wind_eldaba.efx", {372.f, -673.f, 46.f}, 1000.f, {372.f, -673.f, 145.f}},
            {"fx/dust/dust_wind_eldaba.efx", {-242.f, 485.f, -8.f}, 1000.f, {-242.f, 485.f, 90.f}},
        };
        constexpr FxEmitter AMBIENT_MP_DAWNVILLE[] = {
            {"fx/smoke/battlefield_smokebank_S.efx", {620.f, -15642.f, 1.f}, 1000.f, {620.f, -15642.f, 101.f}},
            {"fx/smoke/battlefield_smokebank_S.efx", {2536.f, -15436.f, -84.f}, 1000.f, {2536.f, -15436.f, 15.f}},
            {"fx/smoke/battlefield_smokebank_S.efx", {-302.f, -17245.f, 32.f}, 1000.f, {-302.f, -17245.f, 132.f}},
            {"fx/smoke/battlefield_smokebank_S.efx", {1048.f, -14864.f, -41.f}, 1000.f, {1048.f, -14864.f, 58.f}},
            {"fx/smoke/battlefield_smokebank_S.efx", {-91.f, -14659.f, 6.f}, 1000.f, {-91.f, -14659.f, 105.f}},
            {"fx/smoke/battlefield_smokebank_S.efx", {626.f, -16903.f, 56.f}, 1000.f, {626.f, -16903.f, 156.f}},
            {"fx/smoke/battlefield_smokebank_S.efx", {-1014.f, -17478.f, 37.f}, 1000.f, {-1014.f, -17478.f, 137.f}},
            {"fx/smoke/battlefield_smokebank_S.efx", {-1819.f, -17836.f, -11.f}, 1000.f, {-1819.f, -17836.f, 88.f}},
        };
        constexpr FxEmitter AMBIENT_MP_DECOY[] = {
            {"fx/dust/dust_wind_night.efx", {8823.f, -12338.f, -441.f}, 600.f, {8823.f, -12338.f, -341.f}},
            {"fx/dust/dust_wind_night.efx", {6469.f, -12810.f, -496.f}, 600.f, {6469.f, -12810.f, -396.f}},
            {"fx/dust/dust_wind_night.efx", {7128.f, -13570.f, -489.f}, 600.f, {7128.f, -13570.f, -389.f}},
            {"fx/dust/dust_wind_night.efx", {7008.f, -12849.f, -496.f}, 600.f, {7008.f, -12849.f, -396.f}},
            {"fx/dust/dust_wind_night.efx", {7228.f, -12212.f, -450.f}, 600.f, {7228.f, -12212.f, -350.f}},
            {"fx/dust/dust_wind_night.efx", {7645.f, -12160.f, -396.f}, 600.f, {7645.f, -12160.f, -296.f}},
            {"fx/dust/dust_wind_night.efx", {8566.f, -12053.f, -513.f}, 600.f, {8566.f, -12053.f, -413.f}},
            {"fx/dust/dust_wind_night.efx", {8469.f, -11695.f, -480.f}, 600.f, {8469.f, -11695.f, -380.f}},
            {"fx/dust/dust_wind_night.efx", {8015.f, -11958.f, -474.f}, 600.f, {8015.f, -11958.f, -374.f}},
            {"fx/dust/dust_wind_night.efx", {7642.f, -12674.f, -496.f}, 600.f, {7642.f, -12674.f, -396.f}},
            {"fx/dust/dust_wind_night.efx", {6701.f, -13118.f, -496.f}, 600.f, {6701.f, -13118.f, -396.f}},
            {"fx/dust/dust_wind_night.efx", {7493.f, -13248.f, -480.f}, 600.f, {7493.f, -13248.f, -380.f}},
            {"fx/dust/dust_wind_night.efx", {8316.f, -12883.f, -493.f}, 600.f, {8316.f, -12883.f, -393.f}},
            {"fx/dust/dust_wind_night.efx", {8528.f, -14377.f, -702.f}, 600.f, {8528.f, -14377.f, -602.f}},
            {"fx/dust/dust_wind_night.efx", {9220.f, -13096.f, -508.f}, 600.f, {9220.f, -13096.f, -408.f}},
            {"fx/dust/dust_wind_night.efx", {8674.f, -13699.f, -599.f}, 600.f, {8674.f, -13699.f, -499.f}},
            {"fx/dust/dust_wind_night.efx", {8723.f, -13259.f, -492.f}, 600.f, {8723.f, -13259.f, -392.f}},
            {"fx/dust/dust_wind_night.efx", {9863.f, -13181.f, -499.f}, 600.f, {9863.f, -13181.f, -400.f}},
            {"fx/dust/dust_wind_night.efx", {9590.f, -13828.f, -499.f}, 600.f, {9590.f, -13828.f, -399.f}},
            {"fx/dust/dust_wind_night.efx", {9263.f, -14137.f, -579.f}, 600.f, {9263.f, -14137.f, -479.f}},
            {"fx/dust/dust_wind_night.efx", {9528.f, -12412.f, -520.f}, 600.f, {9528.f, -12412.f, -420.f}},
            {"fx/dust/dust_wind_night.efx", {7244.f, -14192.f, -453.f}, 600.f, {7244.f, -14192.f, -353.f}},
            {"fx/dust/dust_wind_night.efx", {5963.f, -13410.f, -520.f}, 600.f, {5963.f, -13410.f, -420.f}},
            {"fx/dust/dust_wind_night.efx", {6185.f, -14084.f, -341.f}, 600.f, {6185.f, -14084.f, -241.f}},
        };
        constexpr FxEmitter AMBIENT_MP_DOWNTOWN[] = {
            {"fx/misc/snow_light_mp_downtown.efx", {1635.f, -1393.f, 375.f}, 800.f, {1635.f, -1393.f, 475.f}},
            {"fx/misc/snow_wind_cityhall.efx", {414.f, -2231.f, 32.f}, 500.f, {414.f, -2231.f, 132.f}},
            {"fx/misc/snow_wind_cityhall.efx", {1711.f, -2363.f, 129.f}, 500.f, {1711.f, -2363.f, 229.f}},
            {"fx/misc/snow_wind_cityhall.efx", {3635.f, -1707.f, 143.f}, 500.f, {3635.f, -1707.f, 243.f}},
            {"fx/misc/snow_wind_cityhall.efx", {2866.f, 1016.f, 13.f}, 500.f, {2866.f, 1016.f, 113.f}},
            {"fx/misc/snow_wind_cityhall.efx", {2931.f, -271.f, 9.f}, 500.f, {2931.f, -271.f, 109.f}},
            {"fx/misc/snow_wind_cityhall.efx", {354.f, 124.f, 61.f}, 500.f, {354.f, 124.f, 161.f}},
            {"fx/misc/snow_wind_cityhall.efx", {482.f, -1161.f, 3.f}, 500.f, {482.f, -1161.f, 103.f}},
        };
        constexpr FxEmitter AMBIENT_MP_FARMHOUSE[] = {
            {"fx/smoke/battlefield_smokebank_S.efx", {-257.f, 1154.f, 91.f}, 1000.f, {-257.f, 1154.f, 191.f}},
            {"fx/smoke/battlefield_smokebank_S.efx", {-3502.f, -1477.f, -68.f}, 1000.f, {-3502.f, -1477.f, 31.f}},
            {"fx/misc/fogbank_small_duhoc.efx", {-2218.f, -614.f, -56.f}, 2000.f, {-2218.f, -614.f, 43.f}},
            {"fx/misc/fogbank_small_duhoc.efx", {-3140.f, 386.f, -76.f}, 2000.f, {-3140.f, 386.f, 23.f}},
            {"fx/misc/fogbank_small_duhoc.efx", {-2220.f, 1820.f, -38.f}, 2000.f, {-2220.f, 1820.f, 61.f}},
            {"fx/misc/fogbank_small_duhoc.efx", {-848.f, -2588.f, -38.f}, 2000.f, {-848.f, -2588.f, 61.f}},
            {"fx/misc/fogbank_small_duhoc.efx", {-863.f, -1618.f, -38.f}, 2000.f, {-863.f, -1618.f, 61.f}},
            {"fx/misc/fogbank_small_duhoc.efx", {-998.f, -894.f, -38.f}, 2000.f, {-998.f, -894.f, 61.f}},
            {"fx/misc/fogbank_small_duhoc.efx", {-1157.f, -148.f, -38.f}, 2000.f, {-1157.f, -148.f, 61.f}},
            {"fx/misc/fogbank_small_duhoc.efx", {-1134.f, 508.f, -38.f}, 2000.f, {-1134.f, 508.f, 61.f}},
            {"fx/misc/fogbank_small_duhoc.efx", {-998.f, 1051.f, -38.f}, 2000.f, {-998.f, 1051.f, 61.f}},
            {"fx/misc/fogbank_small_duhoc.efx", {-1722.f, 1775.f, -38.f}, 2000.f, {-1722.f, 1775.f, 61.f}},
            {"fx/misc/fogbank_small_duhoc.efx", {-2178.f, -1683.f, -52.f}, 2000.f, {-2178.f, -1683.f, 47.f}},
            {"fx/misc/fogbank_small_duhoc.efx", {-3492.f, -425.f, -69.f}, 2000.f, {-3492.f, -425.f, 30.f}},
        };
        constexpr FxEmitter AMBIENT_MP_HARBOR[] = {
            {"fx/misc/snow_light_mp_downtown.efx", {-11246.f, -7164.f, 450.f}, 600.f, {-11246.f, -7164.f, 530.f}},
            {"fx/misc/snow_light_mp_downtown.efx", {-7352.f, -7369.f, 450.f}, 600.f, {-7352.f, -7369.f, 530.f}},
            {"fx/misc/snow_wind_cityhall.efx", {-8014.f, -8515.f, 34.f}, 1000.f, {-8014.f, -8515.f, 134.f}},
            {"fx/misc/snow_wind_cityhall.efx", {-6776.f, -8657.f, 18.f}, 1000.f, {-6776.f, -8657.f, 118.f}},
            {"fx/misc/snow_wind_cityhall.efx", {-6693.f, -7364.f, 18.f}, 1000.f, {-6693.f, -7364.f, 118.f}},
            {"fx/misc/snow_wind_cityhall.efx", {-7493.f, -7016.f, 2.f}, 1000.f, {-7493.f, -7016.f, 102.f}},
            {"fx/misc/snow_wind_cityhall.efx", {-7977.f, -7641.f, 18.f}, 1000.f, {-7977.f, -7641.f, 118.f}},
            {"fx/misc/snow_wind_cityhall.efx", {-8727.f, -8311.f, 126.f}, 1000.f, {-8727.f, -8311.f, 226.f}},
            {"fx/misc/snow_wind_cityhall.efx", {-8793.f, -7809.f, 14.f}, 1000.f, {-8793.f, -7809.f, 114.f}},
            {"fx/misc/snow_wind_cityhall.efx", {-8814.f, -7145.f, 30.f}, 1000.f, {-8814.f, -7145.f, 130.f}},
            {"fx/misc/snow_wind_cityhall.efx", {-9406.f, -6648.f, 30.f}, 1000.f, {-9406.f, -6648.f, 130.f}},
            {"fx/misc/snow_wind_cityhall.efx", {-9267.f, -8359.f, 30.f}, 1000.f, {-9267.f, -8359.f, 130.f}},
            {"fx/misc/snow_wind_cityhall.efx", {-10115.f, -7554.f, 14.f}, 1000.f, {-10115.f, -7554.f, 114.f}},
            {"fx/misc/snow_wind_cityhall.efx", {-10234.f, -8122.f, 14.f}, 1000.f, {-10234.f, -8122.f, 114.f}},
            {"fx/misc/snow_wind_cityhall.efx", {-9201.f, -6121.f, 15.f}, 1000.f, {-9201.f, -6121.f, 115.f}},
            {"fx/misc/snow_wind_cityhall.efx", {-6245.f, -7070.f, 19.f}, 1000.f, {-6245.f, -7070.f, 119.f}},
            {"fx/misc/snow_wind_cityhall.efx", {-6765.f, -7786.f, 33.f}, 1000.f, {-6765.f, -7786.f, 133.f}},
            {"fx/misc/snow_wind_cityhall.efx", {-7582.f, -8902.f, 7.f}, 1000.f, {-7582.f, -8902.f, 107.f}},
            {"fx/misc/snow_wind_cityhall.efx", {-9590.f, -7220.f, 81.f}, 1000.f, {-9590.f, -7220.f, 181.f}},
            {"fx/misc/snow_wind_cityhall.efx", {-11626.f, -7573.f, 14.f}, 1000.f, {-11626.f, -7573.f, 114.f}},
            {"fx/misc/snow_wind_cityhall.efx", {-11634.f, -8236.f, 15.f}, 1000.f, {-11634.f, -8236.f, 115.f}},
            {"fx/misc/snow_wind_cityhall.efx", {-10975.f, -7170.f, 18.f}, 1000.f, {-10975.f, -7170.f, 118.f}},
            {"fx/misc/snow_wind_cityhall.efx", {-10684.f, -7173.f, 78.f}, 1000.f, {-10684.f, -7173.f, 178.f}},
            {"fx/misc/snow_wind_cityhall.efx", {-9264.f, -8633.f, 174.f}, 1000.f, {-9264.f, -8633.f, 274.f}},
            {"fx/misc/fogbank_small_duhoc.efx", {-8449.f, -8819.f, -33.f}, 2000.f, {-8449.f, -8819.f, 66.f}},
            {"fx/misc/fogbank_small_duhoc.efx", {-9791.f, -8669.f, 8.f}, 2000.f, {-9791.f, -8669.f, 108.f}},
            {"fx/misc/fogbank_small_duhoc.efx", {-9804.f, -8321.f, 8.f}, 2000.f, {-9804.f, -8321.f, 108.f}},
            {"fx/misc/fogbank_small_duhoc.efx", {-8399.f, -7360.f, 28.f}, 2000.f, {-8399.f, -7360.f, 128.f}},
        };
        constexpr FxEmitter AMBIENT_MP_LENINGRAD[] = {
            {"fx/misc/snow_light_mp_downtown.efx", {-75.f, -208.f, 232.f}, 600.f, {-75.f, -208.f, 332.f}},
        };
        constexpr FxEmitter AMBIENT_MP_MATMATA[] = {
            {"fx/dust/dust_wind_brown_thick.efx", {2693.f, 5237.f, -7.f}, 3000.f, {2693.f, 5237.f, 2.f}},
            {"fx/dust/dust_wind_brown_thick.efx", {2758.f, 6086.f, -7.f}, 3000.f, {2758.f, 6086.f, 2.f}},
            {"fx/dust/dust_wind_brown_thick.efx", {3225.f, 5742.f, -7.f}, 3000.f, {3225.f, 5742.f, 2.f}},
            {"fx/dust/dust_wind_brown_thick.efx", {3007.f, 7063.f, 11.f}, 3000.f, {3007.f, 7063.f, 21.f}},
            {"fx/dust/dust_wind_brown_thick.efx", {3700.f, 8032.f, 33.f}, 3000.f, {3700.f, 8032.f, 43.f}},
            {"fx/dust/dust_wind_brown_thick.efx", {4310.f, 7680.f, 33.f}, 3000.f, {4310.f, 7680.f, 43.f}},
            {"fx/dust/dust_wind_brown_thick.efx", {4318.f, 6863.f, 9.f}, 3000.f, {4318.f, 6863.f, 19.f}},
            {"fx/dust/dust_wind_brown_thick.efx", {4323.f, 6232.f, 9.f}, 3000.f, {4323.f, 6232.f, 19.f}},
            {"fx/dust/dust_wind_brown_thick.efx", {5695.f, 7583.f, 9.f}, 3000.f, {5695.f, 7583.f, 19.f}},
            {"fx/dust/dust_wind_brown_thick.efx", {5485.f, 6965.f, 9.f}, 3000.f, {5485.f, 6965.f, 19.f}},
            {"fx/dust/dust_wind_brown_thick.efx", {5387.f, 6282.f, -54.f}, 3000.f, {5387.f, 6282.f, -44.f}},
            {"fx/dust/dust_wind_brown_thick.efx", {4888.f, 5676.f, 9.f}, 3000.f, {4888.f, 5676.f, 19.f}},
            {"fx/dust/dust_wind_brown_thick.efx", {4878.f, 5168.f, 15.f}, 3000.f, {4878.f, 5168.f, 25.f}},
            {"fx/dust/dust_wind_brown_thick.efx", {4875.f, 4523.f, 15.f}, 3000.f, {4875.f, 4523.f, 25.f}},
            {"fx/dust/dust_wind_brown_thick.efx", {6537.f, 6190.f, -17.f}, 3000.f, {6537.f, 6190.f, -7.f}},
        };
        constexpr FxEmitter AMBIENT_MP_RAILYARD[] = {
            {"fx/misc/snow_light_mp_railyard.efx", {-1249.f, 961.f, 355.f}, 600.f, {-1249.f, 961.f, 455.f}},
            {"fx/misc/snow_wind_cityhall.efx", {193.f, 766.f, -12.f}, 600.f, {193.f, 766.f, 87.f}},
            {"fx/misc/snow_wind_cityhall.efx", {-155.f, 1608.f, -10.f}, 600.f, {-155.f, 1608.f, 89.f}},
            {"fx/misc/snow_wind_cityhall.efx", {-493.f, 1639.f, -10.f}, 600.f, {-493.f, 1639.f, 89.f}},
            {"fx/misc/snow_wind_cityhall.efx", {166.f, 65.f, -4.f}, 600.f, {166.f, 65.f, 95.f}},
            {"fx/misc/snow_wind_cityhall.efx", {45.f, -919.f, -10.f}, 600.f, {45.f, -919.f, 89.f}},
            {"fx/misc/snow_wind_cityhall.efx", {-611.f, -263.f, -16.f}, 600.f, {-611.f, -263.f, 83.f}},
            {"fx/misc/snow_wind_cityhall.efx", {201.f, 1434.f, -11.f}, 600.f, {201.f, 1434.f, 88.f}},
            {"fx/misc/snow_wind_cityhall.efx", {-1874.f, 11.f, -7.f}, 600.f, {-1874.f, 11.f, 92.f}},
            {"fx/misc/snow_wind_cityhall.efx", {-1518.f, -927.f, 14.f}, 600.f, {-1518.f, -927.f, 114.f}},
            {"fx/misc/snow_wind_cityhall.efx", {-2270.f, -845.f, -4.f}, 600.f, {-2270.f, -845.f, 95.f}},
            {"fx/misc/fogbank_small_duhoc.efx", {-2514.f, 305.f, 403.f}, 2000.f, {-2514.f, 305.f, 502.f}},
            {"fx/misc/fogbank_small_duhoc.efx", {-2579.f, 91.f, -23.f}, 2000.f, {-2579.f, 91.f, 75.f}},
            {"fx/misc/fogbank_small_duhoc.efx", {-1350.f, 280.f, -15.f}, 2000.f, {-1350.f, 280.f, 82.f}},
            {"fx/misc/fogbank_small_duhoc.efx", {-810.f, 1515.f, -9.f}, 2000.f, {-810.f, 1515.f, 88.f}},
            {"fx/misc/fogbank_small_duhoc.efx", {-194.f, 2170.f, 277.f}, 2000.f, {-194.f, 2170.f, 375.f}},
            {"fx/misc/fogbank_small_duhoc.efx", {-199.f, 2250.f, 21.f}, 2000.f, {-199.f, 2250.f, 119.f}},
            {"fx/misc/fogbank_small_duhoc.efx", {483.f, 1933.f, -7.f}, 2000.f, {483.f, 1933.f, 90.f}},
            {"fx/misc/snow_wind_cityhall.efx", {-217.f, 211.f, 33.f}, 600.f, {-217.f, 211.f, 133.f}},
            {"fx/misc/snow_wind_cityhall.efx", {-1165.f, -483.f, -2.f}, 600.f, {-1165.f, -483.f, 97.f}},
            {"fx/misc/snow_wind_cityhall.efx", {-2343.f, 3342.f, -53.f}, 600.f, {-2343.f, 3342.f, 45.f}},
            {"fx/misc/snow_wind_cityhall.efx", {-1843.f, 2962.f, -58.f}, 600.f, {-1843.f, 2962.f, 40.f}},
            {"fx/misc/snow_wind_cityhall.efx", {-1325.f, 2825.f, -62.f}, 600.f, {-1325.f, 2825.f, 36.f}},
            {"fx/smoke/battlefield_smokebank_S.efx", {-2084.f, 2733.f, -56.f}, 1000.f, {-2084.f, 2733.f, 42.f}},
            {"fx/smoke/battlefield_smokebank_S.efx", {-309.f, 592.f, 63.f}, 1000.f, {-309.f, 592.f, 162.f}},
        };
        constexpr FxEmitter AMBIENT_MP_RHINE[] = {
            {"fx/smoke/battlefield_smokebank_S.efx", {6477.f, 14914.f, 416.f}, 1000.f, {6477.f, 14914.f, 516.f}},
            {"fx/smoke/battlefield_smokebank_S.efx", {7538.f, 15131.f, 416.f}, 1000.f, {7538.f, 15131.f, 516.f}},
            {"fx/smoke/battlefield_smokebank_S.efx", {7046.f, 15279.f, 416.f}, 1000.f, {7046.f, 15279.f, 516.f}},
            {"fx/smoke/battlefield_smokebank_S.efx", {2706.f, 15368.f, 340.f}, 800.f, {2706.f, 15368.f, 440.f}},
            {"fx/smoke/battlefield_smokebank_S.efx", {3841.f, 15623.f, 369.f}, 1000.f, {3841.f, 15623.f, 469.f}},
            {"fx/smoke/battlefield_smokebank_S.efx", {4419.f, 16849.f, 486.f}, 1000.f, {4419.f, 16849.f, 586.f}},
            {"fx/smoke/battlefield_smokebank_S.efx", {5229.f, 16347.f, 510.f}, 1000.f, {5229.f, 16347.f, 610.f}},
            {"fx/smoke/battlefield_smokebank_S.efx", {7848.f, 15866.f, 416.f}, 1000.f, {7848.f, 15866.f, 516.f}},
            {"fx/dust/dust_wind_eldaba.efx", {3444.f, 16002.f, 317.f}, 600.f, {3444.f, 16002.f, 417.f}},
            {"fx/dust/dust_wind_eldaba.efx", {3688.f, 16570.f, 401.f}, 600.f, {3688.f, 16570.f, 501.f}},
            {"fx/dust/dust_wind_eldaba.efx", {6384.f, 16816.f, 477.f}, 600.f, {6384.f, 16816.f, 577.f}},
            {"fx/dust/dust_wind_eldaba.efx", {6659.f, 15766.f, 445.f}, 600.f, {6659.f, 15766.f, 545.f}},
            {"fx/dust/dust_wind_eldaba.efx", {3289.f, 15617.f, 317.f}, 600.f, {3289.f, 15617.f, 417.f}},
            {"fx/dust/dust_wind_eldaba.efx", {5032.f, 14842.f, 391.f}, 600.f, {5032.f, 14842.f, 491.f}},
            {"fx/dust/dust_wind_eldaba.efx", {6771.f, 16499.f, 445.f}, 600.f, {6771.f, 16499.f, 545.f}},
            {"fx/dust/dust_wind_eldaba.efx", {5287.f, 14871.f, 423.f}, 600.f, {5287.f, 14871.f, 523.f}},
            {"fx/dust/dust_wind_eldaba.efx", {4231.f, 17205.f, 529.f}, 600.f, {4231.f, 17205.f, 629.f}},
            {"fx/dust/dust_wind_eldaba.efx", {4311.f, 16284.f, 441.f}, 600.f, {4311.f, 16284.f, 541.f}},
            {"fx/dust/dust_wind_eldaba.efx", {3542.f, 14915.f, 346.f}, 600.f, {3542.f, 14915.f, 446.f}},
            {"fx/dust/dust_wind_eldaba.efx", {6580.f, 15490.f, 386.f}, 600.f, {6580.f, 15490.f, 486.f}},
        };
        constexpr FxEmitter AMBIENT_MP_TOUJANE[] = {
            {"fx/dust/dust_wind_brown.efx", {2027.f, 692.f, 21.f}, 300.f, {2027.f, 692.f, 31.f}},
            {"fx/dust/dust_wind_brown.efx", {1365.f, 3161.f, 69.f}, 300.f, {1365.f, 3161.f, 79.f}},
            {"fx/dust/dust_wind_brown.efx", {1856.f, 2662.f, 69.f}, 300.f, {1856.f, 2662.f, 79.f}},
            {"fx/dust/dust_wind_brown.efx", {2250.f, 2078.f, 69.f}, 300.f, {2250.f, 2078.f, 79.f}},
            {"fx/dust/dust_wind_brown.efx", {2519.f, 1565.f, 69.f}, 300.f, {2519.f, 1565.f, 79.f}},
            {"fx/dust/dust_wind_brown.efx", {2434.f, 890.f, 69.f}, 300.f, {2434.f, 890.f, 79.f}},
            {"fx/dust/dust_wind_brown.efx", {956.f, 1213.f, -20.f}, 300.f, {956.f, 1213.f, -10.f}},
            {"fx/dust/dust_wind_brown.efx", {1480.f, 587.f, -7.f}, 300.f, {1480.f, 587.f, 2.f}},
            {"fx/dust/dust_wind_brown.efx", {988.f, 520.f, -2.f}, 300.f, {988.f, 520.f, 7.f}},
            {"fx/dust/dust_wind_brown.efx", {97.f, 1566.f, 23.f}, 300.f, {97.f, 1566.f, 33.f}},
            {"fx/dust/dust_wind_brown.efx", {7.f, 1042.f, 23.f}, 300.f, {7.f, 1042.f, 33.f}},
            {"fx/dust/dust_wind_brown.efx", {49.f, 550.f, 23.f}, 300.f, {49.f, 550.f, 33.f}},
            {"fx/dust/dust_wind_brown.efx", {1548.f, 1959.f, 36.f}, 300.f, {1548.f, 1959.f, 46.f}},
            {"fx/dust/dust_wind_brown.efx", {1763.f, 1525.f, 70.f}, 300.f, {1763.f, 1525.f, 80.f}},
            {"fx/dust/dust_wind_brown.efx", {1304.f, 1571.f, 13.f}, 300.f, {1304.f, 1571.f, 23.f}},
            {"fx/dust/dust_wind_brown.efx", {898.f, 2122.f, 35.f}, 300.f, {898.f, 2122.f, 45.f}},
            {"fx/dust/dust_wind_brown.efx", {971.f, 2615.f, 67.f}, 300.f, {971.f, 2615.f, 77.f}},
        };
        constexpr FxEmitter AMBIENT_MP_TRAINSTATION[] = {
            {"fx/smoke/battlefield_smokebank_S.efx", {7634.f, -4585.f, -67.f}, 800.f, {7634.f, -4585.f, 31.f}},
            {"fx/smoke/battlefield_smokebank_S.efx", {7735.f, -3622.f, -37.f}, 800.f, {7735.f, -3622.f, 61.f}},
            {"fx/smoke/battlefield_smokebank_S.efx", {7642.f, -1806.f, -23.f}, 800.f, {7642.f, -1806.f, 75.f}},
            {"fx/smoke/battlefield_smokebank_S.efx", {6777.f, -2895.f, -28.f}, 800.f, {6777.f, -2895.f, 70.f}},
            {"fx/smoke/battlefield_smokebank_S.efx", {5880.f, -1868.f, -34.f}, 800.f, {5880.f, -1868.f, 64.f}},
            {"fx/smoke/battlefield_smokebank_S.efx", {3909.f, -3186.f, -24.f}, 800.f, {3909.f, -3186.f, 74.f}},
            {"fx/smoke/battlefield_smokebank_S.efx", {4090.f, -4227.f, -38.f}, 800.f, {4090.f, -4227.f, 61.f}},
            {"fx/smoke/battlefield_smokebank_S.efx", {5938.f, -4938.f, -31.f}, 800.f, {5938.f, -4938.f, 67.f}},
        };

        constexpr MapAmbient STOCK_MAP_AMBIENT[] = {
            {"mp_breakout", AMBIENT_MP_BREAKOUT, std::size(AMBIENT_MP_BREAKOUT)},
            {"mp_brecourt", AMBIENT_MP_BRECOURT, std::size(AMBIENT_MP_BRECOURT)},
            {"mp_burgundy", AMBIENT_MP_BURGUNDY, std::size(AMBIENT_MP_BURGUNDY)},
            {"mp_carentan", AMBIENT_MP_CARENTAN, std::size(AMBIENT_MP_CARENTAN)},
            {"mp_dawnville", AMBIENT_MP_DAWNVILLE, std::size(AMBIENT_MP_DAWNVILLE)},
            {"mp_decoy", AMBIENT_MP_DECOY, std::size(AMBIENT_MP_DECOY)},
            {"mp_downtown", AMBIENT_MP_DOWNTOWN, std::size(AMBIENT_MP_DOWNTOWN)},
            {"mp_farmhouse", AMBIENT_MP_FARMHOUSE, std::size(AMBIENT_MP_FARMHOUSE)},
            {"mp_harbor", AMBIENT_MP_HARBOR, std::size(AMBIENT_MP_HARBOR)},
            {"mp_leningrad", AMBIENT_MP_LENINGRAD, std::size(AMBIENT_MP_LENINGRAD)},
            {"mp_matmata", AMBIENT_MP_MATMATA, std::size(AMBIENT_MP_MATMATA)},
            {"mp_railyard", AMBIENT_MP_RAILYARD, std::size(AMBIENT_MP_RAILYARD)},
            {"mp_rhine", AMBIENT_MP_RHINE, std::size(AMBIENT_MP_RHINE)},
            {"mp_toujane", AMBIENT_MP_TOUJANE, std::size(AMBIENT_MP_TOUJANE)},
            {"mp_trainstation", AMBIENT_MP_TRAINSTATION, std::size(AMBIENT_MP_TRAINSTATION)},
        };

        // signatures of the two gfx-table entries CG_ParseFog itself calls (see Addresses.hpp)
        using R_SetFog_t = int(__cdecl*)(int index, float nearDist, float farDist, int red, int green, int blue,
                                         float density);
        using R_SwitchFog_t = int(__cdecl*)(int index, int timeMs, int durationMs);

        bool fogEnabled = true;
        bool particlesEnabled = true;  // show ambient particles: mutes the demo's own, or gates the replay
        std::string presetName;        // map whose fog to apply; empty = the demo's own fog

        // set while our values (or a forced "off") sit in the renderer, so that returning to the
        // demo's own fog re-parses the fog configstring exactly once
        bool overrideApplied = false;

        std::string appliedName;
        std::string lastWarnedMap;

        const char* GetConfigString(int index)
        {
            const auto& gameState = Structures::GetGameState();
            const auto offset = gameState.stringOffsets[index];
            if (offset <= 0 || offset >= static_cast<int>(sizeof(gameState.stringData)))
            {
                return "";
            }
            return gameState.stringData + offset;
        }

        std::string GetMapName()
        {
            // serverinfo (configstring 0) is "\key\value\..."-formatted
            const auto serverInfo = GetConfigString(0);
            const auto key = std::strstr(serverInfo, "\\mapname\\");
            if (key == nullptr)
            {
                return {};
            }
            const auto value = key + sizeof("\\mapname\\") - 1;
            const auto end = std::strchr(value, '\\');
            return end != nullptr ? std::string(value, end) : std::string(value);
        }

        // exact name first; comp demos mostly run renamed stock-map variants (mp_toujane_fix,
        // mp_matmata_fix, ...) that keep the original's layout and atmosphere, so a stock name
        // that prefixes the actual map name (up to a non-letter boundary) matches too
        template <typename Entry, size_t N>
        const Entry* FindByMapName(const Entry (&table)[N], const std::string& mapName)
        {
            for (const auto& entry : table)
            {
                if (_stricmp(entry.map, mapName.c_str()) == 0)
                {
                    return &entry;
                }
            }
            for (const auto& entry : table)
            {
                const auto len = std::strlen(entry.map);
                if (_strnicmp(entry.map, mapName.c_str(), len) == 0 &&
                    !std::isalpha(static_cast<unsigned char>(mapName[len])))
                {
                    return &entry;
                }
            }
            return nullptr;
        }

        R_SetFog_t GetR_SetFog()
        {
            return *reinterpret_cast<R_SetFog_t*>(Addresses::gfxFunc_R_SetFog);
        }

        R_SwitchFog_t GetR_SwitchFog()
        {
            return *reinterpret_cast<R_SwitchFog_t*>(Addresses::gfxFunc_R_SwitchFog);
        }

        int GetCgTime()
        {
            return *reinterpret_cast<int*>(Addresses::cg_time);
        }

        // -----------------------------------------------------------------------------------------
        // ambient-weather fx replay (see the FxEmitter comment above)
        // -----------------------------------------------------------------------------------------

        constexpr int CS_EFFECT_NAMES = 846;  // + fx id (ids 1..63); each holds a loadfx path
        constexpr int MAX_FX_EFFECTS = 64;

        using FX_PlayEffect_t = int(__fastcall*)(void* fxSystem, void* unusedEdx, int fxHandle, float* origin,
                                                 float* forward);
        using FX_RegisterEffect_t = int(__cdecl*)(const char* name);

        // Vanilla/pub demos carry the ambient emitters as real looped-fx entities in their
        // snapshots. There is no per-entity kill switch, but every looped-fx firing funnels
        // through FX_PlayEffect - so a detour there can mute them, swallowing calls whose handle
        // is one of the game-registered ambient-weather effects.

        FX_PlayEffect_t FX_PlayEffect_Trampoline = nullptr;

        bool muteRealEmitters = false;
        std::vector<int> mutedFxHandles;  // game-registered handles of the ambient-weather effects
        std::string mutedForMap;          // map the handle set was built for; empty = not built

        bool IsAmbientEffectPath(const char* name)
        {
            for (const auto& mapAmbient : STOCK_MAP_AMBIENT)
            {
                for (size_t i = 0; i < mapAmbient.count; i++)
                {
                    if (_stricmp(mapAmbient.emitters[i].efxPath, name) == 0)
                    {
                        return true;
                    }
                }
            }
            return false;
        }

        void SetRealEmittersMuted(bool muted, const std::string& mapName)
        {
            if (!muted)
            {
                muteRealEmitters = false;
                mutedForMap.clear();
                mutedFxHandles.clear();
                return;
            }
            if (mutedForMap != mapName)
            {
                mutedForMap = mapName;
                mutedFxHandles.clear();
                for (int i = 1; i < MAX_FX_EFFECTS; i++)
                {
                    const auto name = GetConfigString(CS_EFFECT_NAMES + i);
                    if (*name != '\0' && IsAmbientEffectPath(name))
                    {
                        const auto handle = *reinterpret_cast<int*>(Addresses::cgs_fxHandles + 4u * i);
                        if (handle != 0)
                        {
                            mutedFxHandles.push_back(handle);
                        }
                    }
                }
                LOG_INFO("Fog override: muting the {} ambient-weather effects carried by the demo",
                         mutedFxHandles.size());
            }
            muteRealEmitters = true;
        }

        int __fastcall FX_PlayEffect_Hook(void* fxSystem, void* unusedEdx, int fxHandle, float* origin, float* forward)
        {
            if (muteRealEmitters)
            {
                for (const auto handle : mutedFxHandles)
                {
                    if (handle == fxHandle)
                    {
                        return 0;
                    }
                }
            }
            return FX_PlayEffect_Trampoline(fxSystem, unusedEdx, fxHandle, origin, forward);
        }

        // the tool's own replay goes through the trampoline so the mute filter cannot swallow it
        // (a swapped-in preset effect may share its handle with a muted one)
        FX_PlayEffect_t GetFXPlayEffect()
        {
            return FX_PlayEffect_Trampoline != nullptr
                       ? FX_PlayEffect_Trampoline
                       : reinterpret_cast<FX_PlayEffect_t>(Addresses::FX_PlayEffect);
        }

        // a map's dominant weather effect - the most used one of its ambient block (snow on the
        // winter maps, dust on the desert ones) - with the firing delay it is tuned for
        const FxEmitter* DominantEmitter(const MapAmbient* ambient)
        {
            const FxEmitter* best = nullptr;
            size_t bestCount = 0;
            for (size_t i = 0; i < ambient->count; i++)
            {
                size_t count = 0;
                for (size_t j = 0; j < ambient->count; j++)
                {
                    if (std::strcmp(ambient->emitters[j].efxPath, ambient->emitters[i].efxPath) == 0)
                    {
                        count++;
                    }
                }
                if (count > bestCount)
                {
                    bestCount = count;
                    best = &ambient->emitters[i];
                }
            }
            return best;
        }

        const MapAmbient* activeAmbient = nullptr;
        std::string activeAmbientKey;          // map (+ style) the state below was built for; empty = inactive
        const char* activeStyleEfx = nullptr;  // when set, replaces every emitter's own effect (preset style)
        float activeStyleDelayMs = 0.0f;
        std::vector<int> emitterNextFireTime;  // per emitter, in cg.time ms
        std::map<std::string, int> fxHandleCache;  // efx path -> fx handle; 0 = load failed (warned)
        bool dumpedDemoEffects = false;

        void ResetEmitters()
        {
            if (activeAmbientKey.empty())
            {
                return;
            }
            activeAmbient = nullptr;
            activeAmbientKey.clear();
            activeStyleEfx = nullptr;
            emitterNextFireTime.clear();
            fxHandleCache.clear();
            dumpedDemoEffects = false;
        }

        int GetFxHandle(const char* efxPath)
        {
            const auto it = fxHandleCache.find(efxPath);
            if (it != fxHandleCache.end())
            {
                return it->second;
            }

            // precached by the demo: take the handle the game registered for that configstring
            int handle = 0;
            for (int i = 1; i < MAX_FX_EFFECTS; i++)
            {
                if (_stricmp(GetConfigString(CS_EFFECT_NAMES + i), efxPath) == 0)
                {
                    handle = *reinterpret_cast<int*>(Addresses::cgs_fxHandles + 4u * i);
                    break;
                }
            }

            if (handle == 0)
            {
                // custom map variants precache their own effect set; register the stock effect
                // ourselves - the same call the game runs when an effect configstring arrives,
                // and the .efx assets always ship in the game's own IWDs
                if (!dumpedDemoEffects)
                {
                    dumpedDemoEffects = true;
                    for (int i = 1; i < MAX_FX_EFFECTS; i++)
                    {
                        const auto name = GetConfigString(CS_EFFECT_NAMES + i);
                        if (*name != '\0')
                        {
                            LOG_DEBUG("Fog override: demo-precached effect {}: '{}'", i, name);
                        }
                    }
                }
                handle = reinterpret_cast<FX_RegisterEffect_t>(Addresses::FX_RegisterEffect)(efxPath);
                if (handle != 0)
                {
                    LOG_INFO("Fog override: effect '{}' not precached by this demo, registered it locally", efxPath);
                }
                else
                {
                    LOG_WARN("Fog override: could not load effect '{}'", efxPath);
                }
            }

            fxHandleCache.emplace(efxPath, handle);
            return handle;
        }

        // styleEfx = nullptr replays each emitter's own effect; a non-null styleEfx (a preset
        // map's dominant weather effect) replaces the effect and firing delay at every anchor
        // point instead - the anchors stay the current map's, their origins are world coordinates
        void ApplyEmitters(const std::string& mapName, const char* styleEfx, float styleDelayMs)
        {
            const auto key = styleEfx != nullptr ? mapName + '|' + styleEfx : mapName;
            if (key != activeAmbientKey)
            {
                ResetEmitters();
                activeAmbientKey = key;
                activeStyleEfx = styleEfx;
                activeStyleDelayMs = styleDelayMs;
                activeAmbient = FindByMapName(STOCK_MAP_AMBIENT, mapName);
                if (activeAmbient != nullptr)
                {
                    emitterNextFireTime.assign(activeAmbient->count, 0);
                    if (styleEfx != nullptr)
                    {
                        LOG_INFO("Fog override: replaying the {} ambient emitter positions of '{}' with '{}'",
                                 activeAmbient->count, activeAmbient->map, styleEfx);
                    }
                    else
                    {
                        LOG_INFO("Fog override: replaying the {} ambient weather emitters of '{}'",
                                 activeAmbient->count, activeAmbient->map);
                    }
                }
            }
            if (activeAmbient == nullptr)
            {
                return;
            }

            const auto fxSystem = *reinterpret_cast<void**>(Addresses::fx_system);
            if (fxSystem == nullptr)
            {
                return;
            }

            const auto now = GetCgTime();
            for (size_t i = 0; i < activeAmbient->count; i++)
            {
                const auto& emitter = activeAmbient->emitters[i];
                auto& nextFire = emitterNextFireTime[i];

                // the engine's own looped-fx schedule: fire once per elapsed interval, catching up
                // in whole steps; when time went backwards (rewind), clamp and fire immediately
                if (now < nextFire)
                {
                    nextFire = now;
                }
                else
                {
                    const auto delay = std::max(
                        1, static_cast<int>(activeStyleEfx != nullptr ? activeStyleDelayMs : emitter.delayMs));
                    if (now - nextFire < delay)
                    {
                        continue;
                    }
                    do
                    {
                        nextFire += delay;
                    } while (now - nextFire >= delay);
                }

                const auto handle = GetFxHandle(activeStyleEfx != nullptr ? activeStyleEfx : emitter.efxPath);
                if (handle == 0)
                {
                    continue;
                }

                glm::vec3 origin(emitter.origin[0], emitter.origin[1], emitter.origin[2]);
                auto forward =
                    glm::normalize(glm::vec3(emitter.target[0], emitter.target[1], emitter.target[2]) - origin);
                GetFXPlayEffect()(fxSystem, nullptr, handle, &origin.x, &forward.x);
            }
        }
    }  // namespace

    void Install()
    {
        HookManager::CreateHook(Addresses::FX_PlayEffect, reinterpret_cast<uintptr_t>(FX_PlayEffect_Hook),
                                reinterpret_cast<uintptr_t*>(&FX_PlayEffect_Trampoline));
    }

    void SetOverride(bool enabled, const std::string& preset, bool particles)
    {
        fogEnabled = enabled;
        particlesEnabled = particles;
        presetName = preset;
        lastWarnedMap.clear();
        // re-resolve next frame: another demo numbers its fx ids differently
        ResetEmitters();
        SetRealEmittersMuted(false, {});
    }

    bool DemoHasFog()
    {
        // CG_ParseFog only sets fog when the configstring has at least two tokens
        auto s = GetConfigString(CS_FOGVARS);
        while (*s == ' ') s++;
        if (*s == '\0')
        {
            return false;
        }
        while (*s != '\0' && *s != ' ') s++;
        while (*s == ' ') s++;
        return *s != '\0';
    }

    std::vector<std::string> GetPresetNames()
    {
        std::vector<std::string> names;
        for (const auto& fog : STOCK_MAP_FOG)
        {
            names.emplace_back(fog.map);
        }
        return names;
    }

    void Apply()
    {
        if (!Structures::IsDemoPlaying())
        {
            return;
        }

        const auto demoHasFog = DemoHasFog();
        const auto mapName = GetMapName();

        // The particle toggle is independent of the fog: demos that carry fog also carry the
        // real server-spawned ambient emitter entities (muted at FX_PlayEffect when unwanted),
        // while fogless (comp) demos get the map's stock emitters replayed client-side. A fog
        // preset from another stock map also carries its atmosphere: the current map's anchor
        // points play the preset map's dominant weather effect instead of their own.
        const auto currentAmbient = FindByMapName(STOCK_MAP_AMBIENT, mapName);
        const auto presetAmbient =
            fogEnabled && !presetName.empty() ? FindByMapName(STOCK_MAP_AMBIENT, presetName) : nullptr;
        const auto presetStyle =
            presetAmbient != nullptr && presetAmbient != currentAmbient ? DominantEmitter(presetAmbient) : nullptr;

        if (!particlesEnabled)
        {
            // no particles at all: mute whatever the demo carries, replay nothing
            SetRealEmittersMuted(demoHasFog, mapName);
            ResetEmitters();
        }
        else if (presetStyle != nullptr && currentAmbient != nullptr)
        {
            // preset atmosphere: mute the real emitters (if any) and replay the swapped style
            SetRealEmittersMuted(demoHasFog, mapName);
            ApplyEmitters(mapName, presetStyle->efxPath, presetStyle->delayMs);
        }
        else if (!demoHasFog)
        {
            // fogless comp demo: the map's own emitters are missing - replay them
            SetRealEmittersMuted(false, mapName);
            ApplyEmitters(mapName, nullptr, 0.0f);
        }
        else
        {
            // vanilla demo showing its own particles
            SetRealEmittersMuted(false, mapName);
            ResetEmitters();
        }

        if (fogEnabled && presetName.empty() && demoHasFog)
        {
            // the demo's own fog: hand the state back to the game's parser once after an override
            // (it re-applies the fog configstring, which our R_SetFog calls had overwritten)
            if (overrideApplied)
            {
                reinterpret_cast<void(__cdecl*)()>(Addresses::CG_ParseFog)();
                overrideApplied = false;
                appliedName.clear();
                LOG_DEBUG("Fog override: restored the demo's own fog");
            }
            return;
        }

        const auto switchFog = GetR_SwitchFog();
        if (switchFog == nullptr)
        {
            return;
        }

        // reapplied every frame: a rewind re-parses the gamestate and puts the demo's fog back,
        // and a vid_restart clears all renderer fog state
        if (!fogEnabled)
        {
            switchFog(0, GetCgTime(), 0);
            overrideApplied = true;
            if (appliedName != "off")
            {
                appliedName = "off";
                LOG_INFO("Fog override: fog forced off");
            }
            return;
        }

        // a chosen preset - or, for demos whose mod suppressed the fog configstring (zPAM comp
        // rules do), the current map's own stock fog
        const auto fogMapName = presetName.empty() ? mapName : presetName;
        const auto fog = FindByMapName(STOCK_MAP_FOG, fogMapName);
        if (fog == nullptr)
        {
            if (lastWarnedMap != fogMapName)
            {
                lastWarnedMap = fogMapName;
                LOG_WARN("Fog override: no stock fog values for map '{}'; leaving fog untouched", fogMapName);
            }
            return;
        }

        const auto setFog = GetR_SetFog();
        if (setFog == nullptr)
        {
            return;
        }

        // what CG_ParseFog does with an exp-fog configstring: slot 1, near 0, far 1, color bytes,
        // density < 1 selects exponential fog; then an instant switch to that slot
        setFog(1, 0.0f, 1.0f, static_cast<int>(fog->r * 255.0f + 0.5f), static_cast<int>(fog->g * 255.0f + 0.5f),
               static_cast<int>(fog->b * 255.0f + 0.5f), fog->density);
        switchFog(1, GetCgTime(), 0);
        overrideApplied = true;

        if (appliedName != fogMapName)
        {
            appliedName = fogMapName;
            LOG_INFO("Fog override: applied the fog of '{}' (density {}, rgb {} {} {})", fogMapName, fog->density,
                     fog->r, fog->g, fog->b);
        }
    }
}  // namespace IWXMVM::IW2::Hooks::Fog
