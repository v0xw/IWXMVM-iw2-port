struct PS_INPUT
{
    float4 pos : POSITION;
    float2 uv : TEXCOORD0;
};

sampler2D colorTex : register(s0);

// x = brightness bias (brightness + 0.5 - 0.5 * contrast), y = contrast, z = desaturation, w = invert
float4 filmParams : register(c0);
// rgb = dark tint
float4 filmDarkTint : register(c1);
// rgb = light tint - dark tint
float4 filmTintDelta : register(c2);

// Reimplementation of CoD4's film tweaks; the math matches the IW3/T4 postfx pixel shader
// (out = (C * biasW + L) * (tintDelta * L + tintBase) + biasRGB) with the CPU-folded
// constants expanded into the original brightness/contrast/desaturation/tint parameters
float4 main(PS_INPUT input) : COLOR
{
    float3 color = saturate(tex2Dlod(colorTex, float4(input.uv, 0, 0)).rgb);
    color = lerp(color, 1.0f - color, filmParams.w);

    float luminance = dot(color, float3(0.299f, 0.587f, 0.114f));
    color = lerp(color, luminance.xxx, filmParams.z);

    float3 tint = filmTintDelta.rgb * luminance + filmDarkTint.rgb;
    return float4(color * filmParams.y * tint + filmParams.x, 1.0f);
}
