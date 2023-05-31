RWTexture2D<float4> RenderTarget : register(u0);

struct Payload {
	float4 unused;
};
struct Attributes {
	float4 unused;
};

[shader("raygeneration")]
void RayGen()
{
	RenderTarget[DispatchRaysIndex().xy] = float4(0.0,1.0,0.0,1.0);
}

[shader("closesthit")]
void ClosestHit(inout Payload payload, in Attributes attrs)
{

}

[shader("anyhit")]
void AnyHit(inout Payload payload, in Attributes attrs)
{

}

[shader("miss")]
void Miss(inout Payload payload)
{

}

[shader("intersection")]
void Intersection()
{

}