#ifndef _ROLLER_TYPES_H
#define _ROLLER_TYPES_H
//-------------------------------------------------------------------------------------------------
#include <stddef.h>
#include <stdint.h>
//-------------------------------------------------------------------------------------------------

#if defined (WIN32) || defined (_WIN32)
#define IS_WINDOWS
#elif defined(__linux__) || defined(linux) || defined(__linux)
#define IS_LINUX
#elif defined(__APPLE__) || defined(__MACH__)
#define IS_MACOS
#endif

//-------------------------------------------------------------------------------------------------

#define ONE_OVER_TRIG_LOOKUP_AY_COUNT 0.00006103515625
#define ONE_OVER_TATN_LOOKUP_AY_COUNT 0.0009765625
#define TWO_PI 6.28318530718
#define HZ_TO_NS(x) ((x) > 0 ? (1000000000UL / (x)) : 0)
#define TRIG_SCALE 25600.0f
#define MAX_CARS 16
#define MAX_PLAYERS MAX_CARS
#define MAX_TRACK_CHUNKS 500
#define MAX_SAMPLES MAX_TRACK_CHUNKS

//-------------------------------------------------------------------------------------------------
// surface type flags
#define SURFACE_FLAG_WALL_31            0x80000000
#define SURFACE_FLAG_BOUNCE_30          0x40000000
#define SURFACE_FLAG_ECHO               0x20000000
#define SURFACE_FLAG_AI_MAX_SPEED       0x10000000
#define SURFACE_FLAG_NO_SPAWN           0x08000000
#define SURFACE_FLAG_PIT_BOX            0x04000000
#define SURFACE_FLAG_PIT                0x02000000
#define SURFACE_FLAG_PIT_ZONE           0x01000000
#define SURFACE_FLAG_AI_FAST_STRAT      0x00800000
#define SURFACE_FLAG_WALL_22            0x00400000
#define SURFACE_FLAG_TRANSPARENT        0x00200000
#define SURFACE_FLAG_BOUNCE_20          0x00100000
#define SURFACE_FLAG_NON_MAGNETIC       0x00080000
#define SURFACE_FLAG_FLIP_VERT          0x00040000
#define SURFACE_FLAG_SKIP_RENDER        0x00020000
#define SURFACE_FLAG_TEXTURE_PAIR       0x00010000
#define SURFACE_FLAG_PREVENT_JUMP       0x00008000
#define SURFACE_FLAG_CONCAVE            0x00004000
#define SURFACE_FLAG_FLIP_BACKFACE      0x00002000
#define SURFACE_FLAG_FLIP_HORIZ         0x00001000
#define SURFACE_FLAG_BACK               0x00000800
#define SURFACE_FLAG_PARTIAL_TRANS      0x00000400
#define SURFACE_FLAG_NO_EXTRAS          0x00000200
#define SURFACE_FLAG_APPLY_TEXTURE      0x00000100
#define SURFACE_MASK_FLAGS              0xFFFFFF00
#define SURFACE_MASK_TEXTURE_INDEX      0x000000FF

//-------------------------------------------------------------------------------------------------
// car-specific texture flags
#define CAR_FLAG_ANMS_LIVERY            0x00008000
#define CAR_FLAG_ANMS_LOOKUP            0x00000200

//-------------------------------------------------------------------------------------------------
// cheat_mode flags
#define CHEAT_MODE_CHEAT_CAR            0x00000001
#define CHEAT_MODE_DEATH_MODE           0x00000002
#define CHEAT_MODE_INVINCIBLE           0x00000004
#define CHEAT_MODE_GRAYSCALE            0x00000008
#define CHEAT_MODE_END_SEQUENCE         0x00000010
#define CHEAT_MODE_RACE_HISTORY         0x00000020
#define CHEAT_MODE_WIDESCREEN           0x00000040
#define CHEAT_MODE_CREDITS              0x00000080
#define CHEAT_MODE_ADVANCED_CARS        0x00000100
#define CHEAT_MODE_KILLER_OPPONENTS     0x00000200
#define CHEAT_MODE_ICY_ROAD             0x00000400
#define CHEAT_MODE_50HZ_TIMER           0x00000800
#define CHEAT_MODE_DOUBLE_TRACK         0x00001000
#define CHEAT_MODE_100HZ_TIMER          0x00002000
#define CHEAT_MODE_CLONES               0x00004000
#define CHEAT_MODE_TINY_CARS            0x00008000
#define CHEAT_MODE_WARP                 0x00010000
#define CHEAT_MODE_FREAKY               0x00020000

//-------------------------------------------------------------------------------------------------
// textures_off flags
#define TEX_OFF_GROUND_TEXTURES         0x00000001
#define TEX_OFF_ROAD_TEXTURES           0x00000002
#define TEX_OFF_WALL_TEXTURES           0x00000004
#define TEX_OFF_CLOUDS                  0x00000008
#define TEX_OFF_HORIZON                 0x00000010
#define TEX_OFF_PANEL_OFF               0x00000020
#define TEX_OFF_CAR_TEXTURES            0x00000040
#define TEX_OFF_BUILDING_TEXTURES       0x00000080
#define TEX_OFF_SHADOWS                 0x00000100
#define TEX_OFF_BUILDINGS               0x00000200
#define TEX_OFF_KMH                     0x00000400
#define TEX_OFF_GLASS_WALLS             0x00000800
#define TEX_OFF_PREMIER_CUP_AVAILABLE   0x00001000
#define TEX_OFF_WIDESCREEN              0x00002000
#define TEX_OFF_CAR_SET_AVAILABLE       0x00008000
#define TEX_OFF_ADVANCED_CARS           0x00010000
#define TEX_OFF_BONUS_CUP_AVAILABLE     0x00020000
#define TEX_OFF_PANEL_RESTRICTED        0x00040000
#define TEX_OFF_PERSPECTIVE_CORRECTION  0x00080000

//-------------------------------------------------------------------------------------------------
//
typedef int8_t   int8;
typedef int16_t  int16;
typedef int32_t  int32;
typedef int64_t  int64;
typedef uint8_t  uint8;
typedef uint16_t uint16;
typedef uint32_t uint32;
typedef uint64_t uint64;

// Windows-specific types when needed
#if !defined(IS_WINDOWS)
    typedef uint32_t DWORD;
    typedef uint16_t WORD;
#endif

//-------------------------------------------------------------------------------------------------

//16.16 fixed point math helpers
typedef int32 fixed16_16;
#define TO_FIXED(x) ((fixed16_16)((x) * 65536.0f))
#define FROM_FIXED(x) ((float)(x) / 65536.0f)
#define GET_HIWORD(x) (((x) >> 16))
#define SET_HIWORD(lval, rval) (lval = (lval & 0x0000FFFF) | ((uint32)(rval) << 16))
#define GET_LOWORD(x) ((x) & 0xFFFF)
#define SET_LOWORD(lval, rval) (lval = (lval & 0xFFFF0000) | ((rval) & 0x0000FFFF))
#define INC_HIWORD(x) (SET_HIWORD(x, GET_HIWORD(x) + 1))
#define DEC_HIWORD(x) (SET_HIWORD(x, GET_HIWORD(x) - 1))
#define INC_LOWORD(x) (SET_LOWORD(x, GET_LOWORD(x) + 1))
#define DEC_LOWORD(x) (SET_LOWORD(x, GET_LOWORD(x) - 1))
#define GET_SHIWORD(x) (int16)GET_HIWORD(x)
#define GET_SLOWORD(x) (int16)GET_LOWORD(x)

#define SET_BYTE1(lval, rval) (lval = (lval & 0xFFFF00FF) | ((rval) & 0x000000FF))
#define GET_HIBYTE(x) (((x) >> 24))

//64-bit helpers
#define SET_BYTE1_64(lval, rval)  (lval = (lval & 0xFFFFFFFFFFFF00FF) | ((rval) & 0x000000000000FF00))
#define SET_LOWORD_64(lval, rval) (lval = (lval & 0xFFFFFFFFFFFF0000) | ((rval) & 0x000000000000FFFF))
#define GET_HIDWORD(x) (((x) >> 32) & 0xFFFFFFFF)
#define SET_HIDWORD(lval, rval) (lval = (lval & 0x00000000FFFFFFFF) | ((uint64)(rval) << 32))
#define GET_LODWORD(x) ((x) & 0xFFFFFFFF)
#define SET_LODWORD(lval, rval) (lval = (lval & 0xFFFFFFFF00000000) | ((rval) & 0x00000000FFFFFFFF))
#define __PAIR64__(high, low) (((uint64)(uint32)(high) << 32) | (uint32)(low))

//other bitwise helpers
#define SET_LOBYTE(lval, rval) (lval = (lval & 0xFFFFFF00) | ((rval) & 0x000000FF))
#define SIGN_EXTEND_24(x) ((int32)((x & 0xFFFFFF) ^ 0x800000) - 0x800000)
#define PACK_24_WITH_TAG(val, tag) ((((uint32)(tag) & 0xFF) << 24) | ((uint32)(val) & 0xFFFFFF))

//-------------------------------------------------------------------------------------------------

typedef struct
{
  float fX;
  float fY;
  float fZ;
} tVec3;

//-------------------------------------------------------------------------------------------------

typedef enum
{
  CAR_AUTO = 0x1,
  CAR_DESILVA = 0x2,
  CAR_PULSE = 0x3,
  CAR_GLOBAL = 0x4,
  CAR_MILLION = 0x5,
  CAR_MISSION = 0x6,
  CAR_ZIZIN = 0x7,
  CAR_REISE = 0x8,
  CAR_F1WACK = 0x9,
  CAR_DEATH = 0xA,
} eCarType;

//-------------------------------------------------------------------------------------------------

#define CAR_DESIGN_AUTO 0x0
#define CAR_DESIGN_DESILVA 0x1
#define CAR_DESIGN_PULSE 0x2
#define CAR_DESIGN_GLOBAL 0x3
#define CAR_DESIGN_MILLION 0x4
#define CAR_DESIGN_MISSION 0x5
#define CAR_DESIGN_ZIZIN 0x6
#define CAR_DESIGN_REISE 0x7
#define CAR_DESIGN_SUICYCO 0x8
#define CAR_DESIGN_MAYTE 0x9
#define CAR_DESIGN_2X4B523P 0xA
#define CAR_DESIGN_TINKLE 0xB
#define CAR_DESIGN_F1WACK 0xC
#define CAR_DESIGN_DEATH 0xD

//-------------------------------------------------------------------------------------------------

typedef struct
{
  uint8 byR;
  uint8 byB;
  uint8 byG;
} tColor;

//-------------------------------------------------------------------------------------------------
#endif
