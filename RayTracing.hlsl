struct Payload {
	float4 unused;
};
struct Attributes {
	float4 unused;
};

[shader("raygeneration")]
void RayGen()
{
}

[shader("closesthit")]
void ClosestHit(inout Payload payload, in Attributes attrs)
{

}

[shader("anyhit")]
void AnyHit(inout Payload payload, in Attributes attrs)
{

}

[shader("intersection")]
void Intersection()
{

}