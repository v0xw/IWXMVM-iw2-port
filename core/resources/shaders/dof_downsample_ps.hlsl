struct PS_INPUT
{
    float4 pos : POSITION;
    float2 uv : TEXCOORD0;
};

sampler2D colorTex : register(s0);
sampler2D depthTex : register(s1);

// x = znear, y = projection depth scale (0.99950027), z = near start, w = near end
float4 dofParams : register(c0);
// x = viewmodel raw depth threshold, yz = depth uv scale (the depth surface can be
// larger than the backbuffer - CoD2 sizes it to the desktop in windowed mode)
float4 dofParams2 : register(c1);

// CoD2 renders with an infinite perspective projection:
//   rawDepth = scale * (1 - znear / viewZ)
float GetViewDistance(float rawDepth)
{
    float d = min(rawDepth / dofParams.y, 0.9999f);
    return dofParams.x / (1.0f - d);
}

float4 main(PS_INPUT input) : COLOR
{
    float3 color = tex2Dlod(colorTex, float4(input.uv, 0, 0)).rgb;

    float rawDepth = tex2Dlod(depthTex, float4(input.uv * dofParams2.yz, 0, 0)).x;
    float dist = GetViewDistance(rawDepth);

    float nearCoc = 1.0f - saturate((dist - dofParams.z) / max(dofParams.w - dofParams.z, 1.0f));

    // The viewmodel is drawn with a compressed depth range; keep it sharp
    if (rawDepth < dofParams2.x)
    {
        nearCoc = 0.0f;
    }

    return float4(color, nearCoc);
}
