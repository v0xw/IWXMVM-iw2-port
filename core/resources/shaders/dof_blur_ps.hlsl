struct PS_INPUT
{
    float4 pos : POSITION;
    float2 uv : TEXCOORD0;
};

sampler2D srcTex : register(s0);

// xy = one blur step in uv space (texel size * direction)
float4 blurStep : register(c0);

// 9-tap separable gaussian; the alpha channel carries the near CoC so that
// blurring it lets the near blur bleed past silhouette edges
float4 main(PS_INPUT input) : COLOR
{
    const float weights[5] = { 0.2270270270f, 0.1945945946f, 0.1216216216f, 0.0540540541f, 0.0162162162f };

    float4 sum = tex2Dlod(srcTex, float4(input.uv, 0, 0)) * weights[0];

    [unroll]
    for (int i = 1; i < 5; i++)
    {
        float2 offset = blurStep.xy * i;
        sum += tex2Dlod(srcTex, float4(input.uv + offset, 0, 0)) * weights[i];
        sum += tex2Dlod(srcTex, float4(input.uv - offset, 0, 0)) * weights[i];
    }

    return sum;
}
