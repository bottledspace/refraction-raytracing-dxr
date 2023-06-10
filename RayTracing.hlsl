struct SceneConstants {
	float4x4 proj_inv;
	float4 camera_loc;
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
Texture2D<float4> EnvironmentMap : register(t3);
SamplerState Sampler : register(s0);

struct Payload {
	float3 color;
	float3 mask;
	bool outside;
	uint count;
};

// Generate a ray in world space for a camera pixel corresponding to an index from the dispatched 2D grid.
inline void GenerateCameraRay(uint2 index, out float3 dir, out float3 origin)
{
	float2 xy = index + 0.5f; // center in the middle of the pixel.
	float2 screenPos = xy / DispatchRaysDimensions().xy * 2.0 - 1.0;

	// Invert Y for DirectX-style coordinates.
	screenPos.y = -screenPos.y;
	
	float4 R = mul(float4(screenPos, 0, 1), sceneConstants.proj_inv);

	// Unproject the pixel coordinate into a ray.
	origin = sceneConstants.camera_loc.xyz;
	dir = normalize(R.xyz);
}

[shader("raygeneration")]
void RayGen()
{
	float3 origin,dir;

	GenerateCameraRay(DispatchRaysIndex().xy, dir, origin);

	RayDesc ray;
	ray.Origin = origin;
	ray.Direction = dir;
	ray.TMin = 0.0001;
	ray.TMax = 100.0;

	Payload payload;
	payload.color = float3(0.0,0.0,0.0);
	payload.mask = float3(1.0,1.0,1.0);
	payload.outside = true;
	payload.count = 0;
	TraceRay(Scene, RAY_FLAG_CULL_BACK_FACING_TRIANGLES, 0xff, 0, 0, 0, ray, payload);

	RenderTarget[DispatchRaysIndex().xy] = float4(payload.color.xyz,1.0);

}

float3 ReflectRay(float3 I, float3 N) {
	return I - 2.0 * dot(N, I) * N;
}

bool RefractRay(out float3 R, float3 I, float3 N, float eta) {
	float k = 1.0 - eta * eta * (1.0 - dot(N, I) * dot(N, I));
	if (k < 0.0)
		return false;
	R = normalize(eta * I - (eta * dot(N, I) + sqrt(k)) * N);
	return true;
}


[shader("closesthit")]
void ClosestHit(inout Payload payload, BuiltInTriangleIntersectionAttributes attrs)
{
	if (payload.count < 5) {
		float3 A = Vertices[Indices[PrimitiveIndex() * 3 + 0]].norm;
		float3 B = Vertices[Indices[PrimitiveIndex() * 3 + 1]].norm;
		float3 C = Vertices[Indices[PrimitiveIndex() * 3 + 2]].norm;
		float3 N = normalize(A + attrs.barycentrics.x*(B-A) + attrs.barycentrics.y*(C-A));

		float3 intersection = WorldRayOrigin() + RayTCurrent() * WorldRayDirection();
		
		float3 dir1;

		float R0 = (0.2 / 2.2) * (0.2 / 2.2);
		float R = R0 * (1.0 - R0) * pow(1.0 - dot(WorldRayDirection(), payload.outside ? N : -N), 5);

		if (RefractRay(dir1, WorldRayDirection(), payload.outside? N:-N, payload.outside ? (1.0/1.3) : 1.3)) {
			RayDesc ray;
			ray.Origin = intersection;
			ray.Direction = dir1;
			ray.TMin = 0.001;
			ray.TMax = 1000.0;

			Payload payload2;
			payload2.count = payload.count+1;
			payload2.mask = float3(1.0,1.0,1.0);
			payload2.outside = !payload.outside;
			TraceRay(Scene, payload2.outside ? RAY_FLAG_CULL_BACK_FACING_TRIANGLES : RAY_FLAG_CULL_FRONT_FACING_TRIANGLES, 0xff, 0, 0, 0, ray, payload2);
			payload.color += (1-R)*payload2.color;
		}

		if (payload.count < 2) {
			RayDesc ray;
			ray.Origin = intersection;
			ray.Direction = normalize(ReflectRay(WorldRayDirection(), payload.outside ? N : -N));
			ray.TMin = 0.001;
			ray.TMax = 1000.0;

			Payload payload2;
			payload2.count = payload.count+1;
			payload2.mask = float3(1.0,1.0,1.0);
			payload2.outside = payload.outside;
			TraceRay(Scene, payload2.outside ? RAY_FLAG_CULL_BACK_FACING_TRIANGLES : RAY_FLAG_CULL_FRONT_FACING_TRIANGLES, 0xff, 0, 0, 0, ray, payload2);
			payload.color += R*payload2.color;
		}
	}
}

[shader("miss")]
void Miss(inout Payload payload)
{
	uint width,height;
	EnvironmentMap.GetDimensions(width, height);
	float3 r = WorldRayDirection();
	float theta = width*(atan2(r.x,r.z) / 3.14159 + 1.0)/2;
	float phi   = height*(acos(r.y) / 3.14159);
	payload.color = payload.mask * EnvironmentMap[float2(theta,phi)];
	((WorldRayDirection().x*WorldRayDirection().y* WorldRayDirection().z >0)? 1.0 : 0.0);
}
