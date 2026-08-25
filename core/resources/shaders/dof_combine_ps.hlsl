struct PS_INPUT
{
    float4 pos : POSITION;
    float2 uv : TEXCOORD0;
};

sampler2D sharpTex : register(s0);
sampler2D blurTex : register(s1);
sampler2D depthTex : register(s2);

// x = znear, y = projection depth scale (0.99950027), z = viewmodel raw depth threshold, w = bias
float4 dofParams : register(c0);
// x = near start, y = near end, z = far start, w = far end
float4 dofRanges : register(c1);
// x = near blur strength (0..1), y = far blur strength (0..1), zw = sharp texel size
float4 dofStrength : register(c2);

float GetViewDistance(float rawDepth)
{
    float d = min(rawDepth / dofParams.y, 0.9999f);
    return dofParams.x / (1.0f - d);
}

float4 main(PS_INPUT input) : COLOR
{
    float3 sharp = tex2Dlod(sharpTex, float4(input.uv, 0, 0)).rgb;
    float4 blurred = tex2Dlod(blurTex, float4(input.uv, 0, 0));

    float rawDepth = tex2Dlod(depthTex, float4(input.uv, 0, 0)).x;
    float dist = GetViewDistance(rawDepth);

    float nearCocSharp = 1.0f - saturate((dist - dofRanges.x) / max(dofRanges.y - dofRanges.x, 1.0f));
    // Expand the near blur outward past silhouette edges (GPU Gems 3, ch. 28)
    float nearCoc = saturate(2.0f * max(blurred.a, nearCocSharp) - nearCocSharp);

    float farCoc = saturate((dist - dofRanges.z) / max(dofRanges.w - dofRanges.z, 1.0f));

    float t = max(nearCoc * dofStrength.x, farCoc * dofStrength.y);

    // Keep the viewmodel sharp
    if (rawDepth < dofParams.z)
    {
        t = 0.0f;
    }

    t = pow(saturate(t), 1.0f / max(dofParams.w, 0.1f));

    // Small blur from the full resolution image, for the sharp-to-blurred transition band
    float2 texel = dofStrength.zw;
    float3 medium = sharp;
    medium += tex2Dlod(sharpTex, float4(input.uv + float2(texel.x, texel.y) * 1.5f, 0, 0)).rgb;
    medium += tex2Dlod(sharpTex, float4(input.uv + float2(-texel.x, texel.y) * 1.5f, 0, 0)).rgb;
    medium += tex2Dlod(sharpTex, float4(input.uv + float2(texel.x, -texel.y) * 1.5f, 0, 0)).rgb;
    medium += tex2Dlod(sharpTex, float4(input.uv + float2(-texel.x, -texel.y) * 1.5f, 0, 0)).rgb;
    medium *= 0.2f;

    float3 result = lerp(sharp, medium, saturate(t * 2.0f));
    result = lerp(result, blurred.rgb, saturate(t * 2.0f - 1.0f));

    return float4(result, 1.0f);
}
