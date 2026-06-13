# libchams v2.0

Chams mod untuk GTA SA Android via AML.

## Pendekatan

Hook `RxOpenGLAllInOneSetRenderCallBack` — inject custom render callback
langsung ke RenderWare OpenGL pipeline node ped (skinned mesh).

## Flow

```
RpSkinGetOpenGLPipeline(rpSKINTYPEMATFX)
  → RxPipelineFindNodeByName("OpenGLAtomicAllInOne")
    → RxOpenGLAllInOneGetRenderCallBack()  ← simpan original
      → RxOpenGLAllInOneSetRenderCallBack(ChamsRenderCB)
```

## Render logic (2 pass)

- **Pass 1:** Depth test OFF → render solid (terlihat through wall)
- **Pass 2:** Depth test ON → render normal di atas

## Offsets (libGTASA.so, verified via nm)

| Fungsi | Offset |
|--------|--------|
| RpSkinGetOpenGLPipeline | 0x1c8758 |
| RxPipelineFindNodeByName | 0x1df9a8 |
| RxOpenGLAllInOneSetRenderCallBack | 0x22302c |
| RxOpenGLAllInOneGetRenderCallBack | 0x223032 |
| RwRenderStateSet | 0x1e2914 |
| RwRenderStateGet | 0x1e2948 |

## Build

Via GitHub Actions (NDK r25c, armeabi-v7a, android-21).

Output: `libs/armeabi-v7a/libchams.so`

## Debug

Cek logcat:
```bash
adb logcat -s libchams
```
