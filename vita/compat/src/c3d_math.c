// sh2-vita: citro3d's matrix functions. Row-major 4x4 matrices whose rows are C3D_FVec (stored
// w,z,y,x). The projections put depth in the 3DS's clip range, -w (near) .. 0 (far), as citro3d
// does; the vertex shaders turn that into GL's range (vita/shaders, "depth map").
#include <citro3d.h>

void Mtx_Transpose(C3D_Mtx* out)
{
	for (int i = 0; i < 4; i++)
		for (int j = i + 1; j < 4; j++) {
			float t = out->r[i].c[3 - j];
			out->r[i].c[3 - j] = out->r[j].c[3 - i];
			out->r[j].c[3 - i] = t;
		}
}

void Mtx_Add(C3D_Mtx* out, const C3D_Mtx* a, const C3D_Mtx* b)
{
	for (int i = 0; i < 16; i++)
		out->m[i] = a->m[i] + b->m[i];
}

void Mtx_Subtract(C3D_Mtx* out, const C3D_Mtx* a, const C3D_Mtx* b)
{
	for (int i = 0; i < 16; i++)
		out->m[i] = a->m[i] - b->m[i];
}

// element (row i, column j); column 0 is x
#define E(mtx, i, j) ((mtx)->r[i].c[3 - (j)])

void Mtx_Multiply(C3D_Mtx* out, const C3D_Mtx* a, const C3D_Mtx* b)
{
	C3D_Mtx t;
	for (int i = 0; i < 4; i++)
		for (int j = 0; j < 4; j++) {
			float s = 0;
			for (int k = 0; k < 4; k++)
				s += E(a, i, k) * E(b, k, j);
			E(&t, i, j) = s;
		}
	*out = t;
}

float Mtx_Inverse(C3D_Mtx* out)
{
	float m[16], inv[16];
	for (int i = 0; i < 4; i++)
		for (int j = 0; j < 4; j++)
			m[i * 4 + j] = E(out, i, j);

	inv[0] = m[5] * m[10] * m[15] - m[5] * m[11] * m[14] - m[9] * m[6] * m[15] + m[9] * m[7] * m[14] + m[13] * m[6] * m[11] - m[13] * m[7] * m[10];
	inv[4] = -m[4] * m[10] * m[15] + m[4] * m[11] * m[14] + m[8] * m[6] * m[15] - m[8] * m[7] * m[14] - m[12] * m[6] * m[11] + m[12] * m[7] * m[10];
	inv[8] = m[4] * m[9] * m[15] - m[4] * m[11] * m[13] - m[8] * m[5] * m[15] + m[8] * m[7] * m[13] + m[12] * m[5] * m[11] - m[12] * m[7] * m[9];
	inv[12] = -m[4] * m[9] * m[14] + m[4] * m[10] * m[13] + m[8] * m[5] * m[14] - m[8] * m[6] * m[13] - m[12] * m[5] * m[10] + m[12] * m[6] * m[9];
	inv[1] = -m[1] * m[10] * m[15] + m[1] * m[11] * m[14] + m[9] * m[2] * m[15] - m[9] * m[3] * m[14] - m[13] * m[2] * m[11] + m[13] * m[3] * m[10];
	inv[5] = m[0] * m[10] * m[15] - m[0] * m[11] * m[14] - m[8] * m[2] * m[15] + m[8] * m[3] * m[14] + m[12] * m[2] * m[11] - m[12] * m[3] * m[10];
	inv[9] = -m[0] * m[9] * m[15] + m[0] * m[11] * m[13] + m[8] * m[1] * m[15] - m[8] * m[3] * m[13] - m[12] * m[1] * m[11] + m[12] * m[3] * m[9];
	inv[13] = m[0] * m[9] * m[14] - m[0] * m[10] * m[13] - m[8] * m[1] * m[14] + m[8] * m[2] * m[13] + m[12] * m[1] * m[10] - m[12] * m[2] * m[9];
	inv[2] = m[1] * m[6] * m[15] - m[1] * m[7] * m[14] - m[5] * m[2] * m[15] + m[5] * m[3] * m[14] + m[13] * m[2] * m[7] - m[13] * m[3] * m[6];
	inv[6] = -m[0] * m[6] * m[15] + m[0] * m[7] * m[14] + m[4] * m[2] * m[15] - m[4] * m[3] * m[14] - m[12] * m[2] * m[7] + m[12] * m[3] * m[6];
	inv[10] = m[0] * m[5] * m[15] - m[0] * m[7] * m[13] - m[4] * m[1] * m[15] + m[4] * m[3] * m[13] + m[12] * m[1] * m[7] - m[12] * m[3] * m[5];
	inv[14] = -m[0] * m[5] * m[14] + m[0] * m[6] * m[13] + m[4] * m[1] * m[14] - m[4] * m[2] * m[13] - m[12] * m[1] * m[6] + m[12] * m[2] * m[5];
	inv[3] = -m[1] * m[6] * m[11] + m[1] * m[7] * m[10] + m[5] * m[2] * m[11] - m[5] * m[3] * m[10] - m[9] * m[2] * m[7] + m[9] * m[3] * m[6];
	inv[7] = m[0] * m[6] * m[11] - m[0] * m[7] * m[10] - m[4] * m[2] * m[11] + m[4] * m[3] * m[10] + m[8] * m[2] * m[7] - m[8] * m[3] * m[6];
	inv[11] = -m[0] * m[5] * m[11] + m[0] * m[7] * m[9] + m[4] * m[1] * m[11] - m[4] * m[3] * m[9] - m[8] * m[1] * m[7] + m[8] * m[3] * m[5];
	inv[15] = m[0] * m[5] * m[10] - m[0] * m[6] * m[9] - m[4] * m[1] * m[10] + m[4] * m[2] * m[9] + m[8] * m[1] * m[6] - m[8] * m[2] * m[5];

	float det = m[0] * inv[0] + m[1] * inv[4] + m[2] * inv[8] + m[3] * inv[12];
	if (fabsf(det) < 1e-12f)
		return 0.0f;
	float r = 1.0f / det;
	for (int i = 0; i < 4; i++)
		for (int j = 0; j < 4; j++)
			E(out, i, j) = inv[i * 4 + j] * r;
	return det;
}

C3D_FVec Mtx_MultiplyFVec4(const C3D_Mtx* mtx, C3D_FVec v)
{
	return FVec4_New(FVec4_Dot(mtx->r[0], v), FVec4_Dot(mtx->r[1], v), FVec4_Dot(mtx->r[2], v), FVec4_Dot(mtx->r[3], v));
}

C3D_FVec Mtx_MultiplyFVec3(const C3D_Mtx* mtx, C3D_FVec v)
{
	return FVec3_New(FVec3_Dot(mtx->r[0], v), FVec3_Dot(mtx->r[1], v), FVec3_Dot(mtx->r[2], v));
}

static void apply(C3D_Mtx* mtx, const C3D_Mtx* t, bool bRightSide)
{
	if (bRightSide)
		Mtx_Multiply(mtx, mtx, t);
	else
		Mtx_Multiply(mtx, t, mtx);
}

void Mtx_Translate(C3D_Mtx* mtx, float x, float y, float z, bool bRightSide)
{
	C3D_Mtx t;
	Mtx_Identity(&t);
	t.r[0].w = x;
	t.r[1].w = y;
	t.r[2].w = z;
	apply(mtx, &t, bRightSide);
}

void Mtx_Scale(C3D_Mtx* mtx, float x, float y, float z)
{
	for (int i = 0; i < 4; i++) {
		mtx->r[i].x *= x;
		mtx->r[i].y *= y;
		mtx->r[i].z *= z;
	}
}

void Mtx_Rotate(C3D_Mtx* mtx, C3D_FVec axis, float angle, bool bRightSide)
{
	axis = FVec3_Normalize(axis);
	float s = sinf(angle), c = cosf(angle), t = 1.0f - c;
	float x = axis.x, y = axis.y, z = axis.z;
	C3D_Mtx r;
	Mtx_Identity(&r);
	r.r[0].x = t * x * x + c;     r.r[0].y = t * x * y - s * z; r.r[0].z = t * x * z + s * y;
	r.r[1].x = t * x * y + s * z; r.r[1].y = t * y * y + c;     r.r[1].z = t * y * z - s * x;
	r.r[2].x = t * x * z - s * y; r.r[2].y = t * y * z + s * x; r.r[2].z = t * z * z + c;
	apply(mtx, &r, bRightSide);
}

void Mtx_RotateX(C3D_Mtx* mtx, float angle, bool bRightSide)
{
	float s = sinf(angle), c = cosf(angle);
	C3D_Mtx r;
	Mtx_Identity(&r);
	r.r[1].y = c; r.r[1].z = -s;
	r.r[2].y = s; r.r[2].z = c;
	apply(mtx, &r, bRightSide);
}

void Mtx_RotateY(C3D_Mtx* mtx, float angle, bool bRightSide)
{
	float s = sinf(angle), c = cosf(angle);
	C3D_Mtx r;
	Mtx_Identity(&r);
	r.r[0].x = c; r.r[0].z = s;
	r.r[2].x = -s; r.r[2].z = c;
	apply(mtx, &r, bRightSide);
}

void Mtx_RotateZ(C3D_Mtx* mtx, float angle, bool bRightSide)
{
	float s = sinf(angle), c = cosf(angle);
	C3D_Mtx r;
	Mtx_Identity(&r);
	r.r[0].x = c; r.r[0].y = -s;
	r.r[1].x = s; r.r[1].y = c;
	apply(mtx, &r, bRightSide);
}

// Depth rows shared by the projections: z_clip in -w .. 0 between near and far.
static void persp_depth(C3D_Mtx* mtx, float near, float far, bool isLeftHanded)
{
	mtx->r[3].z = isLeftHanded ? 1.0f : -1.0f;
	mtx->r[2].z = -mtx->r[3].z * near / (near - far);
	mtx->r[2].w = near * far / (near - far);
}

static void ortho_depth(C3D_Mtx* mtx, float near, float far, bool isLeftHanded)
{
	mtx->r[2].z = isLeftHanded ? 1.0f / (far - near) : 1.0f / (near - far);
	mtx->r[2].w = 0.5f * (near + far) / (near - far) - 0.5f;
	mtx->r[3].w = 1.0f;
}

void Mtx_Ortho(C3D_Mtx* mtx, float left, float right, float bottom, float top, float near, float far, bool isLeftHanded)
{
	Mtx_Zeros(mtx);
	mtx->r[0].x = 2.0f / (right - left);
	mtx->r[0].w = (left + right) / (left - right);
	mtx->r[1].y = 2.0f / (top - bottom);
	mtx->r[1].w = (bottom + top) / (bottom - top);
	ortho_depth(mtx, near, far, isLeftHanded);
}

// The "Tilt" variants turn the picture a quarter turn: the 3DS's screens are portrait panels
// (240 x 400) mounted sideways. The compat layer keeps that layout in every render target and
// turns the final picture back when it shows it (gfx.c).
void Mtx_OrthoTilt(C3D_Mtx* mtx, float left, float right, float bottom, float top, float near, float far, bool isLeftHanded)
{
	Mtx_Zeros(mtx);
	mtx->r[0].y = 2.0f / (top - bottom);
	mtx->r[0].w = (bottom + top) / (bottom - top);
	mtx->r[1].x = 2.0f / (left - right);
	mtx->r[1].w = (left + right) / (right - left);
	ortho_depth(mtx, near, far, isLeftHanded);
}

void Mtx_Persp(C3D_Mtx* mtx, float fovy, float aspect, float near, float far, bool isLeftHanded)
{
	float fovy_tan = tanf(fovy / 2.0f);
	Mtx_Zeros(mtx);
	mtx->r[0].x = 1.0f / (fovy_tan * aspect);
	mtx->r[1].y = 1.0f / fovy_tan;
	persp_depth(mtx, near, far, isLeftHanded);
}

void Mtx_PerspStereo(C3D_Mtx* mtx, float fovy, float aspect, float near, float far, float iod, float screen, bool isLeftHanded)
{
	float fovy_tan = tanf(fovy / 2.0f);
	float fovy_tan_aspect = fovy_tan * aspect;
	float shift = iod / (2.0f * screen);
	Mtx_Zeros(mtx);
	mtx->r[0].x = 1.0f / fovy_tan_aspect;
	mtx->r[0].w = -iod / 2.0f;
	mtx->r[1].y = 1.0f / fovy_tan;
	persp_depth(mtx, near, far, isLeftHanded);
	mtx->r[0].z = -mtx->r[3].z * shift / fovy_tan_aspect;
}

void Mtx_PerspTilt(C3D_Mtx* mtx, float fovx, float invaspect, float near, float far, bool isLeftHanded)
{
	// fovx is the angle across the panel's long side (the 3DS screen's width), invaspect = h / w
	float fovx_tan = tanf(fovx / 2.0f);
	Mtx_Zeros(mtx);
	mtx->r[0].y = 1.0f / fovx_tan;
	mtx->r[1].x = -1.0f / (fovx_tan * invaspect);
	persp_depth(mtx, near, far, isLeftHanded);
}

void Mtx_PerspStereoTilt(C3D_Mtx* mtx, float fovx, float invaspect, float near, float far, float iod, float screen,
                         bool isLeftHanded)
{
	float fovx_tan = tanf(fovx / 2.0f);
	float fovx_tan_invaspect = fovx_tan * invaspect;
	float shift = iod / (2.0f * screen);
	Mtx_Zeros(mtx);
	mtx->r[0].y = 1.0f / fovx_tan;
	mtx->r[1].x = -1.0f / fovx_tan_invaspect;
	mtx->r[1].w = iod / 2.0f;
	persp_depth(mtx, near, far, isLeftHanded);
	mtx->r[1].z = mtx->r[3].z * shift / fovx_tan_invaspect;
}

void Mtx_LookAt(C3D_Mtx* out, C3D_FVec cameraPosition, C3D_FVec cameraTarget, C3D_FVec cameraUpVector, bool isLeftHanded)
{
	C3D_FVec fwd, side, up;
	if (isLeftHanded)
		fwd = FVec3_Normalize(FVec3_Subtract(cameraTarget, cameraPosition));
	else
		fwd = FVec3_Normalize(FVec3_Subtract(cameraPosition, cameraTarget));
	side = FVec3_Normalize(FVec3_Cross(cameraUpVector, fwd));
	up = FVec3_Cross(fwd, side);

	Mtx_Zeros(out);
	out->r[0].x = side.x; out->r[0].y = side.y; out->r[0].z = side.z;
	out->r[1].x = up.x;   out->r[1].y = up.y;   out->r[1].z = up.z;
	out->r[2].x = fwd.x;  out->r[2].y = fwd.y;  out->r[2].z = fwd.z;
	out->r[0].w = -FVec3_Dot(side, cameraPosition);
	out->r[1].w = -FVec3_Dot(up, cameraPosition);
	out->r[2].w = -FVec3_Dot(fwd, cameraPosition);
	out->r[3].w = 1.0f;
}
