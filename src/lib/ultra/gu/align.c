#include "guint.h"

void guAlignF(float mf[4][4], float a, float *xyz)
{
	// Use local copies to avoid modifying the original values; should correct a muzzle flare issue for MagSec 4 and possibly certain other firearms
	float local[3] = { -xyz[0], -xyz[1], -xyz[2] };
	float x, y, z;
	
	static float dtor = 3.1415926f / 180.0f;
	float s, c, h, hinv;

	guNormalize(local);
	x = local[0];
	y = local[1];
	z = local[2];

	a *= dtor;
	s = sinf(a);
	c = cosf(a);
	h = sqrtf(x * x + z * z);

	guMtxIdentF(mf);

	if (h != 0) {
		hinv = 1 / h;

		mf[0][0] = (-z*c - s*y*x) * hinv;
		mf[1][0] = (z*s - c*y*x) * hinv;
		mf[2][0] = -x;
		mf[3][0] = 0;

		mf[0][1] = s*h;
		mf[1][1] = c*h;
		mf[2][1] = -y;
		mf[3][1] = 0;

		mf[0][2] = (c*x - s*y*z) * hinv;
		mf[1][2] = (-s*x - c*y*z) * hinv;
		mf[2][2] = -z;
		mf[3][2] = 0;

		mf[0][3] = 0;
		mf[1][3] = 0;
		mf[2][3] = 0;
		mf[3][3] = 1;
	}
}
