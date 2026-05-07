#include <string.h>
#include <math.h>
#include <PR/gu.h>

static void mtxf_mul(float r[4][4], const float a[4][4], const float b[4][4]) {
    float t[4][4];
    for (int i = 0; i < 4; i++)
        for (int j = 0; j < 4; j++)
            t[i][j] = a[i][0]*b[0][j] + a[i][1]*b[1][j] +
                      a[i][2]*b[2][j] + a[i][3]*b[3][j];
    memcpy(r, t, sizeof(t));
}

void guMtxIdentF(float mf[4][4]) {
    memset(mf, 0, 4 * 4 * sizeof(float));
    mf[0][0] = mf[1][1] = mf[2][2] = mf[3][3] = 1.0f;
}

void guMtxIdent(Mtx *m) {
    float mf[4][4];
    guMtxIdentF(mf);
    guMtxF2L(mf, m);
}

void guMtxF2L(float mf[4][4], Mtx *m) {
    s32 *dst = (s32 *)m->m;
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j += 2) {
            int   word = i * 2 + j / 2;
            s32   e0   = (s32)(mf[i][j]     * 65536.0f);
            s32   e1   = (s32)(mf[i][j + 1] * 65536.0f);
            dst[word]     = (s32)(((u32)(u16)(e0 >> 16) << 16) | (u16)(e1 >> 16));
            dst[8 + word] = (s32)(((u32)(u16)(e0 & 0xffff) << 16) | (u16)(e1 & 0xffff));
        }
    }
}

void guMtxL2F(float mf[4][4], Mtx *m) {
    const s32 *src = (const s32 *)m->m;
    for (int i = 0; i < 4; i++) {
        for (int j = 0; j < 4; j += 2) {
            int  word      = i * 2 + j / 2;
            s32  int_part  = src[word];
            u32  frac_part = (u32)src[8 + word];
            mf[i][j]     = (s32)((int_part  & 0xffff0000u) | (frac_part >> 16))          / 65536.0f;
            mf[i][j + 1] = (s32)(((u32)int_part << 16)     | (frac_part & 0x0000ffffu))  / 65536.0f;
        }
    }
}

void guMtxCatF(float m[4][4], float n[4][4], float r[4][4]) {
    mtxf_mul(r, m, n);
}

void guMtxCat(Mtx *m, Mtx *n, Mtx *res) {
    float mf[4][4], nf[4][4], rf[4][4];
    guMtxL2F(mf, m);
    guMtxL2F(nf, n);
    mtxf_mul(rf, mf, nf);
    guMtxF2L(rf, res);
}

void guScaleF(float mf[4][4], float x, float y, float z) {
    memset(mf, 0, 4 * 4 * sizeof(float));
    mf[0][0] = x;
    mf[1][1] = y;
    mf[2][2] = z;
    mf[3][3] = 1.0f;
}

void guScale(Mtx *m, float x, float y, float z) {
    float mf[4][4];
    guScaleF(mf, x, y, z);
    guMtxF2L(mf, m);
}

void guTranslateF(float mf[4][4], float x, float y, float z) {
    guMtxIdentF(mf);
    mf[0][3] = x;
    mf[1][3] = y;
    mf[2][3] = z;
}

void guTranslate(Mtx *m, float x, float y, float z) {
    float mf[4][4];
    guTranslateF(mf, x, y, z);
    guMtxF2L(mf, m);
}

void guNormalize(float *x, float *y, float *z) {
    float len = sqrtf((*x)*(*x) + (*y)*(*y) + (*z)*(*z));
    if (len > 0.0f) { *x /= len; *y /= len; *z /= len; }
}
