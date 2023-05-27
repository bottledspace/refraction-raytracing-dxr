struct
{
    row_major float4x4 proj;
    row_major float4x4 view;
} constants : b0;

struct PSInput
{
    float4 position : SV_POSITION;
    float4 uv : TEXCOORD;
};

struct PSOutput
{
    float4 color : SV_Target;
};

PSInput VSMain(float3 position : POSITION, float4 uv : TEXCOORD)
{
    PSInput result;
    result.position = float4(position, 1.0);
    result.position = mul(result.position, constants.view);
    result.position = mul(result.position, constants.proj);
    return result;
}

PSOutput PSMain(PSInput input)
{
    PSOutput result;
    result.color = float4(1.0,0.0,1.0,1.0);
    return result;
}