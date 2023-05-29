struct Payload {
	float unused;
};
struct Attributes {
	float unused;
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