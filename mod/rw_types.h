#pragma once
#include <stdint.h>

// RenderWare forward declarations
struct RpAtomic;
struct RwResEntry;
struct RxPipeline;
struct RxPipelineNode;

// RxOpenGLMeshInstanceData (opaque, we only need pointer)
struct RxOpenGLMeshInstanceData;

// RenderWare render state
typedef enum RwRenderState {
    rwRENDERSTATEZTESTENABLE        = 1,
    rwRENDERSTATEZWRITEENABLE       = 2,
    rwRENDERSTATESRCBLEND           = 6,
    rwRENDERSTATEDESTBLEND          = 7,
    rwRENDERSTATEFOGENABLE          = 8,
    rwRENDERSTATESHADEMODE          = 9,
    rwRENDERSTATETEXTURERASTER      = 20,
    rwRENDERSTATECULLMODE           = 21,
    rwRENDERSTATEALPHATESTFUNCTION  = 25,
    rwRENDERSTATEALPHATESTFUNCTIONREF = 26,
} RwRenderState;

typedef enum RwBlendFunction {
    rwBLENDZERO         = 1,
    rwBLENDONE          = 2,
    rwBLENDSRCALPHA     = 5,
    rwBLENDINVSRCALPHA  = 6,
} RwBlendFunction;

// Render callback type
typedef void (*RxOpenGLAllInOneRenderCB)(
    RwResEntry*              repEntry,
    void*                    object,
    unsigned char            type,
    unsigned int             flags);

// RpSkinType enum
typedef enum RpSkinType {
    rpNASKINTYPE    = 0,
    rpSKINTYPEGENERIC = 1,
    rpSKINTYPEMATFX = 2,
    rpSKINTYPETOON  = 3,
} RpSkinType;
