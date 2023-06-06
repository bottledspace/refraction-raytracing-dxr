struct SceneConstants {
	float4x4 mvp;
};
struct Vertex {
	float3 position;
	float3 norm;
	float2 uv;
};

ConstantBuffer<SceneConstants> sceneConstants : register(b0);
RWTexture2D<float4> RenderTarget : register(u0);
RaytracingAccelerationStructure Scene : register(t0);
StructuredBuffer<uint> Indices : register(t1, space0);
StructuredBuffer<Vertex> Vertices : register(t2, space0);

struct Payload {
	float4 color;
	uint count;
};

// Generate a ray in world space for a camera pixel corresponding to an index from the dispatched 2D grid.
inline void GenerateCameraRay(uint2 index, out RayDesc ray)
{
	float2 xy = index + 0.5f; // center in the middle of the pixel.
	float2 screenPos = xy / DispatchRaysDimensions().xy * 2.0 - 1.0;

	// Invert Y for DirectX-style coordinates.
	screenPos.y = -screenPos.y;
	
	// Unproject the pixel coordinate into a ray.
	float4 world = mul(float4(screenPos, 1, 0), sceneConstants.mvp);
	float4 origin = mul(float4(0, 0, 0, 1), sceneConstants.mvp);
	ray.Origin = origin.xyz;
	ray.Direction = normalize(world.xyz - origin.xyz);
}

[shader("raygeneration")]
void RayGen()
{
	RayDesc ray;
	GenerateCameraRay(DispatchRaysIndex().xy, ray);
	ray.TMin = 0.001;
	ray.TMax = 100.0;

	Payload payload;
	payload.color = float4(0.0,0.0,0.0,0.0);
	payload.count = 0;
	TraceRay(Scene, 0, 0xff, 0, 0, 0, ray, payload);

	RenderTarget[DispatchRaysIndex().xy] = float4(payload.color.xyz,1.0);
}

/*[shader("anyhit")]
void AnyHit(inout Payload payload, BuiltInTriangleIntersectionAttributes attrs)
{
	payload.color *= 0.5;

	//TraceRay(Scene, RAY_FLAG_FORCE_OPAQUE, 0xff, 0, 0, 0, ray, payload);

	IgnoreHit();
}*/

float3 ReflectRay(float3 R, float3 N) {
	return 2 * N * dot(N, R) - R;
}

float3 RefractRay(float3 I, float3 N, float mu) {
	float dotNI = dot(N,I);
	return sqrt(1-mu*mu*(1-dotNI*dotNI))*N + mu*(I- dotNI *N);
}

[shader("closesthit")]
void ClosestHit(inout Payload payload, BuiltInTriangleIntersectionAttributes attrs)
{
	if (payload.count < 4) {
		float3 A = Vertices[Indices[PrimitiveIndex() * 3 + 0]].norm;
		float3 B = Vertices[Indices[PrimitiveIndex() * 3 + 1]].norm;
		float3 C = Vertices[Indices[PrimitiveIndex() * 3 + 2]].norm;
		float3 N = normalize(A + attrs.barycentrics.x*(B-A) + attrs.barycentrics.y*(C-A));

		RayDesc ray;
		ray.Origin = WorldRayOrigin() + RayTCurrent() * WorldRayDirection();
		ray.Direction = normalize(RefractRay(WorldRayDirection(), N, 1.33));
		ray.TMin = 0.001;
		ray.TMax = 100.0;

		payload.count++;
		TraceRay(Scene, 0, 0xff, 0, 0, 0, ray, payload);
	}
	//payload.color.xyz *= 0.75;
}

[shader("miss")]
void Miss(inout Payload payload)
{
	payload.color.xyz = (WorldRayDirection().x*WorldRayDirection().y* WorldRayDirection().z >0)? 1.0 : 0.0;
}
