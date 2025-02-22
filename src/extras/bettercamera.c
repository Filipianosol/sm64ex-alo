#ifdef BETTERCAMERA

///Puppycam 2.1 by Fazana
///Extra features and fixes by AloXado320

#include <PR/ultratypes.h>
#include <PR/gbi.h>

#include "sm64.h"
#include "types.h"
#include "game/level_update.h"
#include "bettercamera.h"
#include "audio/external.h"
#include "audio/data.h"
#include "game/game_init.h"
#include "engine/math_util.h"
#include "game/hud.h"
#include "game/print.h"
#include "engine/surface_collision.h"
#include "engine/surface_load.h"
#include "include/text_strings.h"
#include "game/segment2.h"
#include "game/ingame_menu.h"
#include "game/memory.h"
#include "game/object_list_processor.h"
#include "game/object_helpers.h"
#include "behavior_data.h"
#include "game/save_file.h"
#include "game/mario.h"
#include "gfx_dimensions.h"

#include "pc/configfile.h"

#ifdef MOUSE_ACTIONS
#include "pc/controller/controller_mouse.h"
#endif

#define OFFSET 30.0f
#define STEPS 1
#define DECELERATION 0.66f
#define DEADZONE 20
#define SCRIPT_MEMORY_POOL 0x1000

struct gPuppyStruct gPuppyCam;
struct sPuppyVolume *sPuppyVolumeStack[MAX_PUPPYCAM_VOLUMES];
s16 sFloorHeight = 0;
u16 gPuppyVolumeCount = 0;
struct MemoryPool *gPuppyMemoryPool;
s32 gPuppyError = 0;

//Some macros for the sake of basic human sanity.
#define CLAMP(x, low, high)  (((x) > (high)) ? (high) : (((x) < (low)) ? (low) : (x)))
#define ABS(x) ((x) > 0.f ? (x) : -(x))

s16 LENSIN(s16 length, s16 direction)
{
    return (length * sins(direction));
}
s16 LENCOS(s16 length, s16 direction)
{
    return (length * coss(direction));
}

//CONFIG

const u8 optsCameraStr[][32] = {
    { TEXT_OPT_CAMERA },
    { TEXT_OPT_CAMON },
    { TEXT_OPT_CAMMOUSE },
    { TEXT_OPT_INVERTX },
    { TEXT_OPT_INVERTY },
    { TEXT_OPT_CAMX },
    { TEXT_OPT_CAMY },
    { TEXT_OPT_CAMC },
    { TEXT_OPT_ANALOGUE },
    { TEXT_OPT_CAMSCHEME },
};

static const u8 optsCameraSchemeStr[][64] = {
    { TEXT_OPT_CAM_SCH1 },
    { TEXT_OPT_CAM_SCH2 },
    { TEXT_OPT_CAM_SCH3 },
};

static const u8 *cameraChoicesScheme[] = {
    optsCameraSchemeStr[0],
    optsCameraSchemeStr[1],
    optsCameraSchemeStr[2],
};

static struct Option optsCamera[] = {
    DEF_OPT_TOGGLE( optsCameraStr[1], &configEnableCamera ),
#ifdef MOUSE_ACTIONS
    DEF_OPT_TOGGLE( optsCameraStr[2], &configCameraMouse ),
#endif
    DEF_OPT_TOGGLE( optsCameraStr[3], &configCameraInvertX ),
    DEF_OPT_TOGGLE( optsCameraStr[4], &configCameraInvertY ),
    DEF_OPT_SCROLL( optsCameraStr[5], &configCameraXSens, 1, 100, 1 ),
    DEF_OPT_SCROLL( optsCameraStr[6], &configCameraYSens, 1, 100, 1 ),
    DEF_OPT_SCROLL( optsCameraStr[7], &configCameraAggr, 0, 100, 1 ),
    DEF_OPT_TOGGLE( optsCameraStr[8], &configCameraAnalog ),
    DEF_OPT_CHOICE( optsCameraStr[9], &configCameraScheme, cameraChoicesScheme ),
};

struct SubMenu menuCamera   = DEF_SUBMENU( optsCameraStr[0], optsCamera );

#ifdef TARGET_N64 // TODO: save to eeprom
// BetterCamera settings
bool         configEnableCamera  = true;
bool         configCameraInvertX = true;
bool         configCameraInvertY = true;
unsigned int configCameraXSens   = 100;
unsigned int configCameraYSens   = 100;
unsigned int configCameraAggr    = 50;
bool         configCameraAnalog  = false;
unsigned int configCameraScheme  = 0;
#endif

void puppycam_default_config(void) {
    gPuppyCam.enabled = configEnableCamera;
#ifdef MOUSE_ACTIONS
    gPuppyCam.mouse = configCameraMouse;
#endif
    gPuppyCam.options.invertX = configCameraInvertX;
    gPuppyCam.options.invertY = configCameraInvertY;
    gPuppyCam.options.sensitivityX = configCameraXSens;
    gPuppyCam.options.sensitivityY = configCameraYSens;
    gPuppyCam.options.turnAggression = configCameraAggr;
    gPuppyCam.options.analogue = configCameraAnalog;
    gPuppyCam.options.inputType = configCameraScheme;
}

//Initial setup. Ran at the beginning of the game and never again.
void puppycam_boot(void)
{
    gPuppyCam.zoomPoints[0] = 600;
    gPuppyCam.zoomPoints[1] = 1000;
    gPuppyCam.zoomPoints[2] = 1500;
    gPuppyCam.povHeight = 125;
    gPuppyCam.stick2[0] = 0;
    gPuppyCam.stick2[1] = 0;
    gPuppyCam.stickN[0] = 0;
    gPuppyCam.stickN[1] = 0;
    gPuppyMemoryPool = mem_pool_init(MAX_PUPPYCAM_VOLUMES * sizeof(struct sPuppyVolume), MEMORY_POOL_LEFT);
    gPuppyVolumeCount = 0;

    //puppycam_get_save();
}

//Called when an instant warp is done.
void puppycam_warp(f32 displacementX, f32 displacementY, f32 displacementZ)
{
    gPuppyCam.pos[0] += displacementX;
    gPuppyCam.pos[1] += displacementY;
    gPuppyCam.pos[2] += displacementZ;
    gPuppyCam.targetFloorHeight += displacementY;
    gPuppyCam.lastTargetFloorHeight += displacementY;
    gPuppyCam.floorY[0] += displacementY;
    gPuppyCam.floorY[1] += displacementY;
}

///CUTSCENE

void puppycam_activate_cutscene(s32 (*scene)(), s32 lockinput)
{
    gPuppyCam.cutscene = 1;
    gPuppyCam.sceneTimer = 0;
    gPuppyCam.sceneFunc = scene;
    gPuppyCam.sceneInput = lockinput;
}

static void puppycam_process_cutscene(void)
{
    if (gPuppyCam.cutscene)
    {
    if ((gPuppyCam.sceneFunc)() == 1)
    {
        gPuppyCam.cutscene = 0;
        gPuppyCam.sceneInput = 0;
        gPuppyCam.flags = gPuppyCam.intendedFlags;
    }
    gPuppyCam.sceneTimer++;
    }
}

//extern struct SaveBuffer gSaveBuffer;

///CORE

//Just a function that sets a bunch of camera values to 0. It's a function because it's got shared functionality.
void puppycam_reset_values(void)
{
    gPuppyCam.posHeight[0] = 0;
    gPuppyCam.posHeight[1] = 0;
    gPuppyCam.swimPitch = 0;
    gPuppyCam.edgePitch = 0;
    gPuppyCam.moveZoom = 0;
    gPuppyCam.floorY[0] = 0;
    gPuppyCam.floorY[1] = 0;
}

//Set up values. Runs on level load.
void puppycam_init(void)
{

    if (gMarioState->marioObj)
        gPuppyCam.targetObj = gMarioState->marioObj;
    gPuppyCam.targetObj2 = NULL;

    gPuppyCam.intendedFlags = PUPPYCAM_BEHAVIOUR_DEFAULT;

    if (gCurrLevelNum == LEVEL_PSS || (gCurrLevelNum == LEVEL_TTM && gCurrAreaIndex == 2) || (gCurrLevelNum == LEVEL_CCM && gCurrAreaIndex == 2))
        gPuppyCam.intendedFlags |= PUPPYCAM_BEHAVIOUR_SLIDE_CORRECTION;
    gPuppyCam.flags = gPuppyCam.intendedFlags;
    gPuppyCam.zoom = gPuppyCam.zoomPoints[1];
    gPuppyCam.zoomSet = 1;
    gPuppyCam.zoomTarget = gPuppyCam.zoom;
    gPuppyCam.yaw = gMarioState->faceAngle[1]+0x8000;
    gPuppyCam.yawTarget = gPuppyCam.yaw;
    gPuppyCam.pitch = 0x3800;
    gPuppyCam.pitchTarget = gPuppyCam.pitch;
    gPuppyCam.yawAcceleration = 0;
    gPuppyCam.pitchAcceleration = 0;
    gPuppyCam.shakeFrames = 0;
    gPuppyCam.shake[0] = 0;
    gPuppyCam.shake[1] = 0;
    gPuppyCam.shake[2] = 0;
    gPuppyCam.pos[0] = 0;
    gPuppyCam.pos[1] = 0;
    gPuppyCam.pos[2] = 0;
    gPuppyCam.focus[0] = 0;
    gPuppyCam.focus[1] = 0;
    gPuppyCam.focus[2] = 0;
    gPuppyCam.pan[0] = 0;
    gPuppyCam.pan[1] = 0; //gMarioState->pos[1];
    gPuppyCam.pan[2] = 0;
    gPuppyCam.targetFloorHeight = gPuppyCam.pan[1];
    gPuppyCam.lastTargetFloorHeight = gMarioState->pos[1];
    gPuppyCam.opacity = 255;
    gPuppyCam.framesSinceC[0] = 10; //This just exists to stop input type B being stupid.
    gPuppyCam.framesSinceC[1] = 10; //This just exists to stop input type B being stupid.
    gPuppyCam.mode3Flags = PUPPYCAM_MODE3_ZOOMED_MED;
    puppycam_reset_values();
}

void puppycam_input_pitch(void)
{
    //f32 ivY = ((gPuppyCam.options.invertY*2)-1)*(gPuppyCam.options.sensitivityY/100.f);

    if (gPuppyCam.flags & PUPPYCAM_BEHAVIOUR_PITCH_ROTATION)
    {
        //Handles vertical inputs.
        if (gPlayer1Controller->buttonDown & U_CBUTTONS || gPuppyCam.stick2[1] != 0)
            gPuppyCam.pitchAcceleration -= 50*(gPuppyCam.options.sensitivityY/100.f);
        else
        if (gPlayer1Controller->buttonDown & D_CBUTTONS || gPuppyCam.stick2[1] != 0)
            gPuppyCam.pitchAcceleration += 50*(gPuppyCam.options.sensitivityY/100.f);
        else
            gPuppyCam.pitchAcceleration = approach_f32_asymptotic(gPuppyCam.pitchAcceleration, 0, DECELERATION);

        gPuppyCam.pitchAcceleration = CLAMP(gPuppyCam.pitchAcceleration, -100, 100);

        //When Mario's moving, his pitch is clamped pretty aggressively, so this exists so you can shift your view up and down momentarily at an actually usable range, rather than the otherwise baby range.
        if (gMarioState->action & ACT_FLAG_MOVING && (gPuppyCam.pitch >= 0x3800 || gPuppyCam.pitch <= 0x2000))
            gPuppyCam.moveFlagAdd = 8;
    }
}

void puppycam_input_zoom(void)
{
    
    //Update zoom if it doesn't match with the zoom target point
    if (gPuppyCam.zoomTarget != gPuppyCam.zoomPoints[gPuppyCam.zoomSet]) {
        gPuppyCam.zoomTarget = gPuppyCam.zoomPoints[gPuppyCam.zoomSet];
    }

    //Handles R button zooming.
    if (gPlayer1Controller->buttonPressed & R_TRIG && gPuppyCam.flags & PUPPYCAM_BEHAVIOUR_ZOOM_CHANGE)
    {
        gPuppyCam.zoomSet++;

        if (gPuppyCam.zoomSet >= 3)
            gPuppyCam.zoomSet = 0;

        gPuppyCam.zoomTarget = gPuppyCam.zoomPoints[gPuppyCam.zoomSet];
        play_sound(SOUND_MENU_CLICK_CHANGE_VIEW,gGlobalSoundSource);
    }
}

void puppycam_input_centre(void)
{
    s32 inputDefault = L_TRIG;
    if (gPuppyCam.options.inputType == PUPPYCAM_INPUT_TYPE_CLASSIC)
        inputDefault = R_TRIG;
    //Handles L button centering.
    if (gPlayer1Controller->buttonPressed & inputDefault && gPuppyCam.flags & PUPPYCAM_BEHAVIOUR_YAW_ROTATION &&
    !(gPuppyCam.flags & PUPPYCAM_BEHAVIOUR_INPUT_8DIR) && !(gPuppyCam.flags & PUPPYCAM_BEHAVIOUR_INPUT_4DIR) && !(gPlayer1Controller->buttonDown & U_JPAD))
    {
        gPuppyCam.yawTarget = gMarioState->faceAngle[1]+0x8000;
        play_sound(SOUND_MENU_CLICK_CHANGE_VIEW,gGlobalSoundSource);
    }
}

//The default control scheme. Hold the button down to turn the camera, and double tap to turn quickly.
static void puppycam_input_hold_preset1(f32 ivX)
{
    if (!gPuppyCam.options.analogue && gPlayer1Controller->buttonPressed & L_CBUTTONS && gPuppyCam.framesSinceC[0] <= 5)
    {
        gPuppyCam.yawTarget -= 0x4000*ivX;
        play_sound(SOUND_MENU_CAMERA_ZOOM_IN, gGlobalSoundSource);
    }
    else
    if (!gPuppyCam.options.analogue && gPlayer1Controller->buttonPressed & R_CBUTTONS && gPuppyCam.framesSinceC[1] <= 5)
    {
        gPuppyCam.yawTarget += 0x4000*ivX;
        play_sound(SOUND_MENU_CAMERA_ZOOM_IN, gGlobalSoundSource);
    }

    if ((gPlayer1Controller->buttonDown & L_CBUTTONS && !gPuppyCam.options.analogue) || gPuppyCam.stick2[0] != 0)
    {
        gPuppyCam.yawAcceleration -= 50*(gPuppyCam.options.sensitivityX/100.f);
        gPuppyCam.framesSinceC[0] = 0;
    }
    else
    if ((gPlayer1Controller->buttonDown & R_CBUTTONS && !gPuppyCam.options.analogue) || gPuppyCam.stick2[0] != 0)
    {
        gPuppyCam.yawAcceleration += 50*(gPuppyCam.options.sensitivityX/100.f);
        gPuppyCam.framesSinceC[1] = 0;
    }
    else
        gPuppyCam.yawAcceleration = approach_f32_asymptotic(gPuppyCam.yawAcceleration, 0, DECELERATION);
}

//An alternative control scheme, hold the button down to turn the camera, or press it once to turn it quickly.
static void puppycam_input_hold_preset2(f32 ivX)
{
    //These set the initial button press.
    if (gPlayer1Controller->buttonPressed & L_CBUTTONS)
    {
        gPuppyCam.framesSinceC[0] = 0;
    }

    if (gPlayer1Controller->buttonPressed & R_CBUTTONS)
    {
        gPuppyCam.framesSinceC[1] = 0;
    }

    //These handle when you release the button
    if ((!(gPlayer1Controller->buttonDown & L_CBUTTONS)) && gPuppyCam.framesSinceC[0] <= 5)
    {
        gPuppyCam.yawTarget -= 0x3000*ivX;
        play_sound(SOUND_MENU_CAMERA_ZOOM_IN, gGlobalSoundSource);
        gPuppyCam.framesSinceC[0] = 6;
    }

    if ((!(gPlayer1Controller->buttonDown & R_CBUTTONS)) && gPuppyCam.framesSinceC[1] <= 5)
    {
        gPuppyCam.yawTarget += 0x3000*ivX;
        play_sound(SOUND_MENU_CAMERA_ZOOM_IN, gGlobalSoundSource);
        gPuppyCam.framesSinceC[1] = 6;
    }

    //Handles continuous movement as normal, as long as the button's held.
    if (gPlayer1Controller->buttonDown & L_CBUTTONS)
    {
        gPuppyCam.yawAcceleration -= 10*(gPuppyCam.options.sensitivityX/100.f);
    }
    else
    if (gPlayer1Controller->buttonDown & R_CBUTTONS)
    {
        gPuppyCam.yawAcceleration += 10*(gPuppyCam.options.sensitivityX/100.f);
    }
    else
        gPuppyCam.yawAcceleration = approach_f32_asymptotic(gPuppyCam.yawAcceleration, 0, DECELERATION);
}

//Another alternative control scheme. This one aims to mimic the parallel camera scheme down to the last bit from the original game.
static void puppycam_input_hold_preset3(f32 ivX)
{
    f32 stickMag[2] = {gPlayer1Controller->rawStickX*0.65f, gPlayer1Controller->rawStickY*0.2f};
    //Just in case it happens to be nonzero.
    gPuppyCam.yawAcceleration = 0;

    //In theory this shouldn't be necessary, but it's nice to cover all bases.
    if (!(gPuppyCam.flags & PUPPYCAM_BEHAVIOUR_YAW_ROTATION))
        return;

    if (gPuppyCam.mode3Flags & PUPPYCAM_MODE3_ZOOMED_IN)
    {
        //f32 ivY = ((gPuppyCam.options.invertY*2)-1)*(gPuppyCam.options.sensitivityY/100.f);
        gPuppyCam.flags &= ~PUPPYCAM_BEHAVIOUR_COLLISION;

        //Handles continuous movement as normal, as long as the button's held.
        if (ABS(gPlayer1Controller->rawStickX) > DEADZONE)
        {
            gPuppyCam.yawAcceleration -= (gPuppyCam.options.sensitivityX/100.f)*stickMag[0];
        }
        else
            gPuppyCam.yawAcceleration = approach_f32_asymptotic(gPuppyCam.yawAcceleration, 0, DECELERATION);

        if (ABS(gPlayer1Controller->rawStickY) > DEADZONE)
        {
            gPuppyCam.pitchAcceleration -= (gPuppyCam.options.sensitivityY/100.f)*stickMag[1];
        }
        else
            gPuppyCam.pitchAcceleration = approach_f32_asymptotic(gPuppyCam.pitchAcceleration, 0, DECELERATION);
    }
    else
    {
        if (gPlayer1Controller->buttonPressed & L_TRIG)
        {
            if (gPuppyCam.yawTarget % 0x2000)
                gPuppyCam.yawTarget += 0x2000 - gPuppyCam.yawTarget % 0x2000;
        }

        if (gPuppyCam.mode3Flags & PUPPYCAM_MODE3_ZOOMED_MED)
            gPuppyCam.pitchTarget = approach_s32(gPuppyCam.pitchTarget, 0x3800, 0x200, 0x200);
        if (gPuppyCam.mode3Flags & PUPPYCAM_MODE3_ZOOMED_OUT)
            gPuppyCam.pitchTarget = approach_s32(gPuppyCam.pitchTarget, 0x3000, 0x200, 0x200);

        if (gPlayer1Controller->buttonPressed & L_CBUTTONS)
        {
            gPuppyCam.yawTarget -= 0x2000*ivX;
            play_sound(SOUND_MENU_CAMERA_TURN,gGlobalSoundSource);
        }
        if (gPlayer1Controller->buttonPressed & R_CBUTTONS)
        {
            gPuppyCam.yawTarget += 0x2000*ivX;
            play_sound(SOUND_MENU_CAMERA_TURN,gGlobalSoundSource);
        }
    }

    //Handles zooming in. Works just like vanilla.
    if (gPlayer1Controller->buttonPressed & U_CBUTTONS)
    {
        if ((gPuppyCam.mode3Flags & PUPPYCAM_MODE3_ZOOMED_MED) && !(gMarioState->action & ACT_FLAG_AIR) && !(gMarioState->action & ACT_FLAG_SWIMMING))
        {
            gPuppyCam.mode3Flags |= PUPPYCAM_MODE3_ZOOMED_IN;
            gPuppyCam.mode3Flags &= ~PUPPYCAM_MODE3_ZOOMED_MED;
            gPuppyCam.zoomTarget = 200;
            gPuppyCam.mode3Flags |= PUPPYCAM_MODE3_ENTER_FIRST_PERSON;

            play_sound(SOUND_MENU_CAMERA_ZOOM_IN, gGlobalSoundSource);
        }
        else
        if (gPuppyCam.mode3Flags & PUPPYCAM_MODE3_ZOOMED_OUT)
        {
            gPuppyCam.mode3Flags |= PUPPYCAM_MODE3_ZOOMED_MED;
            gPuppyCam.mode3Flags &= ~PUPPYCAM_MODE3_ZOOMED_OUT;
            gPuppyCam.zoomTarget = gPuppyCam.zoomPoints[1];

            play_sound(SOUND_MENU_CAMERA_ZOOM_IN, gGlobalSoundSource);
        }
    }
    else //Otherwise handle zooming out.
    if (gPlayer1Controller->buttonPressed & D_CBUTTONS)
    {
        if (gPuppyCam.mode3Flags & PUPPYCAM_MODE3_ZOOMED_MED)
        {
            gPuppyCam.mode3Flags |= PUPPYCAM_MODE3_ZOOMED_OUT;
            gPuppyCam.mode3Flags &= ~PUPPYCAM_MODE3_ZOOMED_MED;
            gPuppyCam.zoomTarget = gPuppyCam.zoomPoints[2];

            play_sound(SOUND_MENU_CAMERA_ZOOM_OUT, gGlobalSoundSource);
        }
    }
    if (gPlayer1Controller->buttonPressed & D_CBUTTONS || gPlayer1Controller->buttonPressed & B_BUTTON || gPlayer1Controller->buttonPressed & A_BUTTON)
    {
        if (gPuppyCam.mode3Flags & PUPPYCAM_MODE3_ZOOMED_IN)
        {
            gPuppyCam.mode3Flags |= PUPPYCAM_MODE3_ZOOMED_MED;
            gPuppyCam.mode3Flags &= ~PUPPYCAM_MODE3_ZOOMED_IN;
            gPuppyCam.zoomTarget = gPuppyCam.zoomPoints[1];
            gPuppyCam.mode3Flags &= ~PUPPYCAM_MODE3_ENTER_FIRST_PERSON;

            play_sound(SOUND_MENU_CAMERA_ZOOM_OUT, gGlobalSoundSource);
        }
    }
}

//Handles C Button inputs for modes that have held inputs, rather than presses.
static void puppycam_input_hold(void)
{
    f32 ivX = ((gPuppyCam.options.invertX*2)-1)*(gPuppyCam.options.sensitivityX/100.f);
    f32 ivY = ((gPuppyCam.options.invertY*2)-1)*(gPuppyCam.options.sensitivityY/100.f);
    s8 stickMag[2] = {100, 100};

    //Analogue Camera stuff. If it fails to find an input, then it just sets stickmag to 100, which after calculations means the value goes unchanged.
    if (gPuppyCam.options.analogue && gPuppyCam.options.inputType != PUPPYCAM_INPUT_TYPE_CLASSIC)
    {
        stickMag[0] = gPuppyCam.stick2[0]*1.25f;
        stickMag[1] = gPuppyCam.stick2[1]*1.25f;
    }

    //In theory this shouldn't be necessary, but it's nice to cover all bases.
    if (!(gPuppyCam.flags & PUPPYCAM_BEHAVIOUR_YAW_ROTATION))
        return;

    if (!gPuppyCam.options.analogue)
    {
        switch (gPuppyCam.options.inputType)
        {
            default: puppycam_input_hold_preset1(ivX); puppycam_input_pitch(); puppycam_input_zoom(); puppycam_input_centre(); break;
            case 1: puppycam_input_hold_preset2(ivX); puppycam_input_pitch(); puppycam_input_zoom(); puppycam_input_centre(); break;
            case 2: puppycam_input_hold_preset3(ivX); puppycam_input_centre(); break;
        }
    }
    else
    {
        puppycam_input_hold_preset1(ivX);
    }

    
    gPuppyCam.framesSinceC[0]++;
    gPuppyCam.framesSinceC[1]++;

    gPuppyCam.yawAcceleration = CLAMP(gPuppyCam.yawAcceleration, -100, 100);

    gPuppyCam.yawTarget += (12*gPuppyCam.yawAcceleration*ivX)*(stickMag[0]*0.01f);
    gPuppyCam.pitchTarget += ((4+gPuppyCam.moveFlagAdd)*gPuppyCam.pitchAcceleration*ivY)*(stickMag[1]*0.01f);
}

//Handles C Button inputs for modes that have pressed inputs, rather than held.
static void puppycam_input_press(void)
{
    f32 ivX = ((gPuppyCam.options.invertX*2)-1)*(gPuppyCam.options.sensitivityX/100.f);
    f32 ivY = ((gPuppyCam.options.invertY*2)-1)*(gPuppyCam.options.sensitivityY/100.f);
    s8 stickMag = 0;

    //Analogue Camera stuff. If it fails to find an input, then it just sets stickmag to 100, which after calculations means the value goes unchanged.
    if (gPuppyCam.options.analogue)
        stickMag = gPuppyCam.stick2[0]*1.25f;
    else
        stickMag = 100;

    //Just in case it happens to be nonzero.
    gPuppyCam.yawAcceleration = 0;

    //In theory this shouldn't be necessary, but it's nice to cover all bases.
    if (!(gPuppyCam.flags & PUPPYCAM_BEHAVIOUR_YAW_ROTATION))
        return;

    if ((gPlayer1Controller->buttonPressed & L_CBUTTONS && !gPuppyCam.options.analogue) || (gPuppyCam.stickN[0] == 0 && gPuppyCam.stick2[0] < -DEADZONE))
    {
        gPuppyCam.stickN[0] = 1;
        if (gPuppyCam.flags & PUPPYCAM_BEHAVIOUR_INPUT_8DIR)
            gPuppyCam.yawTarget -= 0x2000*ivX;
        else
            gPuppyCam.yawTarget -= 0x4000*ivX;
        play_sound(SOUND_MENU_CAMERA_ZOOM_IN,gGlobalSoundSource);
    }

    if ((gPlayer1Controller->buttonPressed & R_CBUTTONS && !gPuppyCam.options.analogue) || (gPuppyCam.stickN[0] == 0 && gPuppyCam.stick2[0] > DEADZONE))
    {
        gPuppyCam.stickN[0] = 1;
        if (gPuppyCam.flags & PUPPYCAM_BEHAVIOUR_INPUT_8DIR)
            gPuppyCam.yawTarget += 0x2000*ivX;
        else
            gPuppyCam.yawTarget += 0x4000*ivX;
        play_sound(SOUND_MENU_CAMERA_ZOOM_IN,gGlobalSoundSource);
    }
    puppycam_input_pitch();
    gPuppyCam.pitchTarget += ((4+gPuppyCam.moveFlagAdd)*gPuppyCam.pitchAcceleration*ivY)*(stickMag*0.01f);
}

#ifdef MOUSE_ACTIONS
//Mouse control support for PC Platforms
static void puppycam_input_mouse(void) {
    f32 ivX = ((gPuppyCam.options.invertX*2)-1)*(gPuppyCam.options.sensitivityX/100.f);
    f32 ivY = ((gPuppyCam.options.invertY*2)-1)*(gPuppyCam.options.sensitivityY/100.f);

    if (configMouse && gPuppyCam.mouse && gPuppyCam.options.inputType != PUPPYCAM_INPUT_TYPE_CLASSIC) {
        gMouseHasCenterControl = TRUE;

        gPuppyCam.yawTarget += ivX * gMouseXPos * 16;
        gPuppyCam.pitchTarget += ivY * gMouseYPos * 16;
    } else {
        gMouseHasCenterControl = FALSE;
    }
}
#endif

static void puppycam_view_panning(void)
{
    f32 panFloor, panMulti;
    s32 expectedPanX, expectedPanZ;
    s32 height = gPuppyCam.targetObj->oPosY;
    s32 panEx = (gPuppyCam.zoomTarget >= 1000) * 250; //Removes the basic panning when idling if the zoom level is at the closest.
    f32 slideSpeed = 1;

    panMulti = CLAMP(gPuppyCam.zoom/(f32)gPuppyCam.zoomPoints[2], 0.f, 1.f);

    if (gPuppyCam.flags & PUPPYCAM_BEHAVIOUR_PANSHIFT && gMarioState->action != ACT_HOLDING_BOWSER && gMarioState->action != ACT_SLEEPING && gMarioState->action != ACT_START_SLEEPING)
    {
        if (gMarioState->action & ACT_FLAG_BUTT_OR_STOMACH_SLIDE)
            slideSpeed = 10;

        expectedPanX = LENSIN(panEx+(200*(gMarioState->forwardVel/32.f)), gMarioState->faceAngle[1])*panMulti;
        expectedPanZ = LENCOS(panEx+(200*(gMarioState->forwardVel/32.f)), gMarioState->faceAngle[1])*panMulti;

        gPuppyCam.pan[0] = approach_f32_asymptotic(gPuppyCam.pan[0], expectedPanX, 0.02f*slideSpeed);
        gPuppyCam.pan[2] = approach_f32_asymptotic(gPuppyCam.pan[2], expectedPanZ, 0.02f*slideSpeed);
        if (gMarioState->vel[1] == 0.0f)
        {
            panFloor = CLAMP(find_floor_height((s16)(gPuppyCam.targetObj->oPosX+expectedPanX),(s16)(gPuppyCam.targetObj->oPosY + 200),
            (s16)(gPuppyCam.targetObj->oPosZ+expectedPanZ)),gPuppyCam.targetObj->oPosY-50,gPuppyCam.targetObj->oPosY+50);
            //If the floor is lower than 50 units below Mario, then ignore the Y value and tilt the camera instead.
            if (panFloor <= gPuppyCam.targetObj->oPosY-50)
            {
                panFloor = gPuppyCam.targetObj->oPosY;
                gPuppyCam.edgePitch = approach_s32(gPuppyCam.edgePitch, -0x500, 0x80, 0x80);
            }
            else
            {
                gPuppyCam.edgePitch = approach_s32(gPuppyCam.edgePitch, 0, 0x80, 0x80);
            }

            gPuppyCam.pan[1] = approach_f32_asymptotic(gPuppyCam.pan[1], panFloor-height, 0.25f);
        }
        else
            gPuppyCam.pan[1] = approach_f32_asymptotic(gPuppyCam.pan[1], 0, 0.5f);
    }
    else
    {
        gPuppyCam.pan[0] = 0;
        gPuppyCam.pan[1] = 0;
        gPuppyCam.pan[2] = 0;
    }
}

//Sets the
static void puppycam_view_height_offset(void)
{
    s16 floorTemp;
    s16 tempDist = sqrtf((gPuppyCam.pos[0] - gPuppyCam.focus[0]) * (gPuppyCam.pos[0] - gPuppyCam.focus[0]) + (gPuppyCam.pos[1] - gPuppyCam.focus[1]) *
                         (gPuppyCam.pos[1] - gPuppyCam.focus[1]) + (gPuppyCam.pos[2] - gPuppyCam.focus[2]) * (gPuppyCam.pos[2] - gPuppyCam.focus[2]));

    floorTemp = find_floor_height(gPuppyCam.targetObj->oPosX, gPuppyCam.targetObj->oPosY+50, gPuppyCam.targetObj->oPosZ);
    if (floorTemp > gPuppyCam.targetObj->oPosY - 50 && !(gMarioState->action & ACT_FLAG_SWIMMING_OR_FLYING))
    {
        gPuppyCam.posHeight[0] = approach_f32_asymptotic(gPuppyCam.posHeight[0],floorTemp-gPuppyCam.targetFloorHeight,0.05f);
        //if (gPuppyCam.posHeight[0]-gPuppyCam.shake[1] - gPuppyCam.floorY[1] < floorTemp)
        //    gPuppyCam.posHeight[0] = floorTemp-gPuppyCam.shake[1]+gPuppyCam.povHeight - gPuppyCam.floorY[1];
    }
    else
    {
            gPuppyCam.posHeight[0] = approach_f32_asymptotic(gPuppyCam.posHeight[0],0,0.1f);
    }


    floorTemp = find_floor_height(gPuppyCam.targetObj->oPosX + LENSIN(tempDist,gPuppyCam.yaw), gPuppyCam.targetObj->oPosY+50, gPuppyCam.targetObj->oPosZ + LENCOS(tempDist,gPuppyCam.yaw));
    if (floorTemp > gPuppyCam.targetObj->oPosY - 50 && !(gMarioState->action & ACT_FLAG_SWIMMING_OR_FLYING) && gPuppyCam.collisionDistance != gPuppyCam.zoomTarget)
    {
        gPuppyCam.posHeight[1] = approach_f32_asymptotic(gPuppyCam.posHeight[1],floorTemp-gPuppyCam.targetFloorHeight,0.05f);
        //if (gPuppyCam.posHeight[1]-gPuppyCam.shake[1] - gPuppyCam.floorY[0] < floorTemp)
        //    gPuppyCam.posHeight[1] = floorTemp-gPuppyCam.shake[1]+gPuppyCam.povHeight - gPuppyCam.floorY[0];
    }
    else
    {
            gPuppyCam.posHeight[1] = approach_f32_asymptotic(gPuppyCam.posHeight[1],0,0.1f);
    }
}

//Raycasting
s32 ray_surface_intersect(Vec3f orig, Vec3f dir, f32 dir_length, struct Surface *surface, Vec3f hit_pos, f32 *length)
{
    Vec3f v0, v1, v2, e1, e2, h, s, q;
    f32 a, f, u, v;
    Vec3f add_dir;
    Vec3f norm;

    //Ignore certain surface types.
    if (surface->type == SURFACE_INTANGIBLE || surface->type == SURFACE_WALL_MISC
        || surface->type == SURFACE_VANISH_CAP_WALLS || surface->flags & SURFACE_FLAG_NO_CAM_COLLISION)
        return FALSE;

    // Get surface normal and some other stuff
    norm[0] = 0;
    norm[1] = surface->normal.y;
    norm[2] = 0;
    vec3f_mul(norm,OFFSET);

    vec3s_to_vec3f(v0, surface->vertex1);
    vec3s_to_vec3f(v1, surface->vertex2);
    vec3s_to_vec3f(v2, surface->vertex3);

    vec3f_add(v0, norm);
    vec3f_add(v1, norm);
    vec3f_add(v2, norm);

    vec3f_dif(e1, v1, v0);
    vec3f_dif(e2, v2, v0);

    vec3f_cross(h, dir, e2);

    // Check if we're perpendicular from the surface
    a = vec3f_dot(e1, h);
    if (a > -0.00001f && a < 0.00001f)
        return FALSE;

    // Check if we're making contact with the surface
    f = 1.0f / a;

    vec3f_dif(s, orig, v0);
    u = f * vec3f_dot(s, h);
    if (u < 0.0f || u > 1.0f)
        return FALSE;

    vec3f_cross(q, s, e1);
    v = f * vec3f_dot(dir, q);
    if (v < 0.0f || u + v > 1.0f)
        return FALSE;

    // Get the length between our origin and the surface contact point
    *length = f * vec3f_dot(e2, q);
    if (*length <= 0.00001 || *length > dir_length)
        return FALSE;

    // Successful contact
    vec3f_copy(add_dir, dir);
    vec3f_mul(add_dir, *length);
    vec3f_sum(hit_pos, orig, add_dir);
    return TRUE;
}

void find_surface_on_ray_list(struct SurfaceNode *list, Vec3f orig, Vec3f dir, f32 dir_length, struct Surface **hit_surface, Vec3f hit_pos, f32 *max_length)
{
    s32 hit;
    f32 length;
    Vec3f chk_hit_pos;
    f32 top, bottom;

    // Get upper and lower bounds of ray
    if (dir[1] >= 0.0f)
    {
        top = orig[1] + dir[1] * dir_length;
        bottom = orig[1];
    }
    else
    {
        top = orig[1];
        bottom = orig[1] + dir[1] * dir_length;
    }

    // Iterate through every surface of the list
    for (; list != NULL; list = list->next)
    {
        // Reject surface if out of vertical bounds
        if (list->surface->lowerY > top || list->surface->upperY < bottom)
            continue;

        // Check intersection between the ray and this surface
        if ((hit = ray_surface_intersect(orig, dir, dir_length, list->surface, chk_hit_pos, &length)) != 0)
        {
            if (length <= *max_length)
            {
                *hit_surface = list->surface;
                vec3f_copy(hit_pos, chk_hit_pos);
                *max_length = length;
            }
        }
    }
}


void find_surface_on_ray_cell(s16 cellX, s16 cellZ, Vec3f orig, Vec3f normalized_dir, f32 dir_length, struct Surface **hit_surface, Vec3f hit_pos, f32 *max_length)
{
	// Skip if OOB
	if (cellX >= 0 && cellX <= NUM_CELLS_INDEX && cellZ >= 0 && cellZ <= NUM_CELLS_INDEX)
	{
		// Iterate through each surface in this partition
		if (normalized_dir[1] > -0.99999f)
		{
			find_surface_on_ray_list(gStaticSurfacePartition[cellZ][cellX][SPATIAL_PARTITION_CEILS].next, orig, normalized_dir, dir_length, hit_surface, hit_pos, max_length);
			find_surface_on_ray_list(gDynamicSurfacePartition[cellZ][cellX][SPATIAL_PARTITION_CEILS].next, orig, normalized_dir, dir_length, hit_surface, hit_pos, max_length);
		}
		if (normalized_dir[1] < 0.99999f)
		{
			find_surface_on_ray_list(gStaticSurfacePartition[cellZ][cellX][SPATIAL_PARTITION_FLOORS].next, orig, normalized_dir, dir_length, hit_surface, hit_pos, max_length);
			find_surface_on_ray_list(gDynamicSurfacePartition[cellZ][cellX][SPATIAL_PARTITION_FLOORS].next, orig, normalized_dir, dir_length, hit_surface, hit_pos, max_length);
		}
		find_surface_on_ray_list(gStaticSurfacePartition[cellZ][cellX][SPATIAL_PARTITION_WALLS].next, orig, normalized_dir, dir_length, hit_surface, hit_pos, max_length);
		find_surface_on_ray_list(gDynamicSurfacePartition[cellZ][cellX][SPATIAL_PARTITION_WALLS].next, orig, normalized_dir, dir_length, hit_surface, hit_pos, max_length);
	}
}

void find_surface_on_ray(Vec3f orig, Vec3f dir, struct Surface **hit_surface, Vec3f hit_pos)
{
    f32 max_length;
    s16 cellZ, cellX, cellPrevX, cellPrevZ;
    f32 fCellZ, fCellX;
    f32 dir_length;
    Vec3f normalized_dir;
    f32 step, dx, dz;
    u32 i;

    // Set that no surface has been hit
    *hit_surface = NULL;
    vec3f_sum(hit_pos, orig, dir);

    // Get normalized direction
    dir_length = vec3f_length(dir);
    max_length = dir_length;
    vec3f_copy(normalized_dir, dir);
    vec3f_normalize(normalized_dir);

    // Get our cell coordinate
    fCellX = (orig[0] + LEVEL_BOUNDARY_MAX) / CELL_SIZE;
    fCellZ = (orig[2] + LEVEL_BOUNDARY_MAX) / CELL_SIZE;
    cellX = (s16)fCellX;
    cellZ = (s16)fCellZ;

    // Don't do DDA if straight down
    if (normalized_dir[1] >= 0.99999f || normalized_dir[1] <= -0.99999f)
    {
		find_surface_on_ray_cell(cellX, cellZ, orig, normalized_dir, dir_length, hit_surface, hit_pos, &max_length);
		return;
	}

    // Get cells we cross using DDA
    if (ABS(dir[0]) >= ABS(dir[2]))
        step = STEPS*ABS(dir[0]) / CELL_SIZE;
    else
        step = STEPS*ABS(dir[2]) / CELL_SIZE;

    dx = dir[0] / step / CELL_SIZE;
    dz = dir[2] / step / CELL_SIZE;

    for (i = 0; i < step && *hit_surface == NULL; i++)
    {
		find_surface_on_ray_cell(cellX, cellZ, orig, normalized_dir, dir_length, hit_surface, hit_pos, &max_length);

        // Move cell coordinate
        fCellX += dx;
        fCellZ += dz;
        cellPrevX = cellX;
        cellPrevZ = cellZ;
        cellX = (s16)fCellX;
        cellZ = (s16)fCellZ;

        if ((cellPrevX != cellX) && (cellPrevZ != cellZ))
        {
            find_surface_on_ray_cell(cellX, cellPrevZ, orig, normalized_dir, dir_length, hit_surface, hit_pos, &max_length);
            find_surface_on_ray_cell(cellPrevX, cellZ, orig, normalized_dir, dir_length, hit_surface, hit_pos, &max_length);
        }
    }
}

//Checks the bounding box of a puppycam volume. If it's inside, then set the pointer to the current index.
static s32 puppycam_check_volume_bounds(struct sPuppyVolume *volume, s32 index)
{
    s32 rel[3];
    s32 pos[2];
    f32 distCheck;

    if (sPuppyVolumeStack[index]->room != gMarioCurrentRoom && sPuppyVolumeStack[index]->room != -1)
        return FALSE;

    if (sPuppyVolumeStack[index]->shape == PUPPYVOLUME_SHAPE_BOX)
    {
        //Fetch the relative position. to the triggeree.
        rel[0] = sPuppyVolumeStack[index]->pos[0] - gPuppyCam.targetObj->oPosX;
        rel[1] = sPuppyVolumeStack[index]->pos[1] - gPuppyCam.targetObj->oPosY;
        rel[2] = sPuppyVolumeStack[index]->pos[2] - gPuppyCam.targetObj->oPosZ;
        //Use the dark, forbidden arts of trig to rotate the volume.
        pos[0] = rel[2] * sins(sPuppyVolumeStack[index]->rot) + rel[0] * coss(sPuppyVolumeStack[index]->rot);
        pos[1] = rel[2] * coss(sPuppyVolumeStack[index]->rot) - rel[0] * sins(sPuppyVolumeStack[index]->rot);

        //Now compare values.
        if (-sPuppyVolumeStack[index]->radius[0] < pos[0] && pos[0] < sPuppyVolumeStack[index]->radius[0] &&
            -sPuppyVolumeStack[index]->radius[1] < rel[1] && rel[1] < sPuppyVolumeStack[index]->radius[1] &&
            -sPuppyVolumeStack[index]->radius[2] < pos[1] && pos[1] < sPuppyVolumeStack[index]->radius[2])
        {
            *volume = *sPuppyVolumeStack[index];
            return TRUE;
        }
    }
    else
    if (sPuppyVolumeStack[index]->shape == PUPPYVOLUME_SHAPE_CYLINDER)
    {
        //s16 dir;
        f32 dist;
        rel[0] = sPuppyVolumeStack[index]->pos[0] - gPuppyCam.targetObj->oPosX;
        rel[1] = sPuppyVolumeStack[index]->pos[1] - gPuppyCam.targetObj->oPosY;
        rel[2] = sPuppyVolumeStack[index]->pos[2] - gPuppyCam.targetObj->oPosZ;
        dist = sqrtf((rel[0] * rel[0]) + (rel[2] * rel[2]));

        distCheck = (dist < sPuppyVolumeStack[index]->radius[0]);

        if (-sPuppyVolumeStack[index]->radius[1] < rel[1] && rel[1] < sPuppyVolumeStack[index]->radius[1] && distCheck)
        {
            *volume = *sPuppyVolumeStack[index];
            return TRUE;
        }

    }

    return FALSE;
}

void puppycam_projection_behaviours(void)
{
    f32 turnRate = 1;

    //This will only be executed if Mario's the target. If it's not, it'll reset the
    if (gPuppyCam.targetObj == gMarioState->marioObj)
    {
        if (gPuppyCam.options.turnAggression > 0 && gPuppyCam.flags & PUPPYCAM_BEHAVIOUR_TURN_HELPER && !(gPuppyCam.flags & PUPPYCAM_BEHAVIOUR_INPUT_8DIR) &&
        gMarioState->vel[1] == 0.0f && !(gPuppyCam.flags & PUPPYCAM_BEHAVIOUR_INPUT_4DIR) && gPuppyCam.options.inputType != PUPPYCAM_INPUT_TYPE_CLASSIC)
        {//Holy hell this is getting spicy.
            //With turn aggression enabled, or if Mario's sliding, adjust the camera view behind mario.
            if (gPuppyCam.options.turnAggression > 0 || gMarioState->action & ACT_FLAG_BUTT_OR_STOMACH_SLIDE)
            {
                if (gMarioState->action & ACT_FLAG_BUTT_OR_STOMACH_SLIDE)
                    turnRate = 4; //If he's sliding, do it 4x as fast.
                //The deal here, is if Mario's moving, or he's sliding and the camera's within 90 degrees behind him, it'll auto focus behind him, with an intensity based on the camera's centre speed.
                //It also scales with forward velocity, so it's a gradual effect as he speeds up.
                if ((ABS(gPlayer1Controller->rawStickX) > 20 && !(gMarioState->action & ACT_FLAG_BUTT_OR_STOMACH_SLIDE)) ||
                    (gMarioState->action & ACT_FLAG_BUTT_OR_STOMACH_SLIDE &&
                     (s16)ABS(((gPuppyCam.yaw + 0x8000) % 0xFFFF - 0x8000) - ((gMarioState->faceAngle[1]) % 0xFFFF - 0x8000)) < 0x3000 ))
                gPuppyCam.yawTarget  = gMarioState->faceAngle[1]+0x8000 - approach_s32((s16)(gMarioState->faceAngle[1]+0x8000 - gPuppyCam.yawTarget), 0,
                ((gPuppyCam.options.turnAggression*10)*ABS(gMarioState->forwardVel/32) * ABS(gPlayer1Controller->rawStickX/80.0f)*turnRate),
                ((gPuppyCam.options.turnAggression*10)*ABS(gMarioState->forwardVel/32) * ABS(gPlayer1Controller->rawStickX/80.0f)*turnRate));
            }
        }
        else
        { //If none of the above is true, it'll attempt to do this instead.
            //If the camera's in these modes, snap the yaw to prevent desync.
            if (gPuppyCam.flags & PUPPYCAM_BEHAVIOUR_INPUT_8DIR)
            {
                if (gPuppyCam.yawTarget % 0x2000)
                    gPuppyCam.yawTarget += 0x2000 - gPuppyCam.yawTarget % 0x2000;
            }
            if (gPuppyCam.flags & PUPPYCAM_BEHAVIOUR_INPUT_4DIR)
            {
                if (gPuppyCam.yawTarget % 0x4000)
                    gPuppyCam.yawTarget += 0x4000 - gPuppyCam.yawTarget % 0x4000;
            }
        }

        //This is the base floor height when stood on the ground. It's used to set a baseline for where the camera sits while Mario remains a height from this point, so it keeps a consistent motion.
        gPuppyCam.targetFloorHeight = CLAMP(find_floor_height(gPuppyCam.targetObj->oPosX, gPuppyCam.targetObj->oPosY, gPuppyCam.targetObj->oPosZ), gPuppyCam.targetObj->oPosY-350, gPuppyCam.targetObj->oPosY+300);

        if (gMarioState->vel[1] <= 0.0f)
            gPuppyCam.lastTargetFloorHeight = CLAMP(approach_f32_asymptotic(gPuppyCam.lastTargetFloorHeight,gPuppyCam.targetFloorHeight, 0.1f), gPuppyCam.targetObj->oPosY-350, gPuppyCam.targetObj->oPosY+300);

        if (gMarioState->action == ACT_SLEEPING || gMarioState->action == ACT_START_SLEEPING)
            gPuppyCam.zoom = approach_f32_asymptotic(gPuppyCam.zoom,gPuppyCam.zoomPoints[0],0.01f);
        else
        if (gMarioState->action & ACT_FLAG_SWIMMING && gMarioState->waterLevel-100 - gMarioState->pos[1] > 5)
        {
            //When moving underwater, the camera will zoom in on Mayro.
            gPuppyCam.zoom = approach_f32_asymptotic(gPuppyCam.zoom, MAX(gPuppyCam.zoomTarget/1.5f, gPuppyCam.zoomPoints[0]), 0.2f);
        }
        else
            gPuppyCam.zoom = approach_f32_asymptotic(gPuppyCam.zoom,gPuppyCam.zoomTarget,0.2f);

        //Attempts at automatic adjustment that only apply when moving or jumping.
        if (gMarioState->action & ACT_FLAG_MOVING || gMarioState->action & ACT_FLAG_AIR || (gMarioState->action & ACT_FLAG_SWIMMING && !gMarioState->waterLevel-100 - gMarioState->pos[1] > 5 && gMarioState->forwardVel != 0.0f))
        {
        //Clamp the height when moving. You can still look up and down to a reasonable degree but it readjusts itself the second you let go.
        if (gPuppyCam.pitchTarget > 0x3800)
            gPuppyCam.pitchTarget = approach_f32_asymptotic(gPuppyCam.pitchTarget, 0x3800, 0.2f);

        if (gPuppyCam.pitchTarget < 0x2000)
            gPuppyCam.pitchTarget = approach_f32_asymptotic(gPuppyCam.pitchTarget, 0x2000, 0.2f);

        /*//Will tilt the camera just a wip thing though, doesn't work too well but will hopefully replace view_height_offset eventually.
        if (gPuppyCam.flags & PUPPYCAM_BEHAVIOUR_HEIGHT_HELPER && gMarioState->floor && gMarioState->action & ACT_FLAG_MOVING)
            gPuppyCam.terrainPitch = approach_f32_asymptotic(gPuppyCam.terrainPitch, find_floor_slope(gMarioState, 0), (gMarioState->intendedMag/32)*0.2);
        else
            gPuppyCam.terrainPitch = approach_f32_asymptotic(gPuppyCam.terrainPitch, 0, 0.2);*/
        }

        //Applies a light outward zoom to the camera when moving. Sets it back to 0 when not moving.
        if (gMarioState->forwardVel > 0)
        {
            gPuppyCam.moveZoom = approach_f32(gPuppyCam.moveZoom, 100.0f*(gMarioState->forwardVel/32.0f), gMarioState->forwardVel/10, gMarioState->forwardVel/10);
        }
        else
        {
            gPuppyCam.moveZoom = approach_f32(gPuppyCam.moveZoom, 0, 5, 5);
        }

        //Zooms the camera in further when underwater.
        if (gPuppyCam.pitch > 0x38C0 && ABS(gPuppyCam.swimPitch) < 100)
        {
            gPuppyCam.zoom = approach_f32_asymptotic((f32)gPuppyCam.zoom, 250.0f, CLAMP((f32)((gPuppyCam.pitch-0x38C0)/3072.0f), 0.0f, 1.0f));
        }

        if (!(gMarioState->action & ACT_FLAG_SWIMMING))
        {
            gPuppyCam.floorY[0] = CLAMP(gPuppyCam.targetObj->oPosY - gPuppyCam.lastTargetFloorHeight, 0, 300);
            gPuppyCam.floorY[1] = CLAMP(gPuppyCam.targetObj->oPosY - gPuppyCam.lastTargetFloorHeight, 0, 350);
            gPuppyCam.swimPitch = approach_f32_asymptotic(gPuppyCam.swimPitch,0,0.2f);
        }
        else
        {
            gPuppyCam.floorY[0] = 0;
            gPuppyCam.floorY[1] = 0;
            gPuppyCam.targetFloorHeight = gPuppyCam.targetObj->oPosY;
            gPuppyCam.lastTargetFloorHeight = gPuppyCam.targetObj->oPosY;
            gPuppyCam.yawTarget  = gMarioState->faceAngle[1]+0x8000 - approach_s32((s16)(gMarioState->faceAngle[1]+0x8000 - gPuppyCam.yawTarget), 0,
            1000*(gMarioState->forwardVel/32), 1000*(gMarioState->forwardVel/32));
            if (gMarioState->waterLevel-100 - gMarioState->pos[1] > 5 && gPuppyCam.flags & PUPPYCAM_BEHAVIOUR_PITCH_ROTATION)
                gPuppyCam.swimPitch = approach_f32_asymptotic(gPuppyCam.swimPitch,gMarioState->faceAngle[0]/10,0.05f);
            else
                gPuppyCam.swimPitch = approach_f32_asymptotic(gPuppyCam.swimPitch,0,0.2f);
        }

        //This sets the view offset from Mario. It accentuates a bit further when moving.
        puppycam_view_panning();

        //This sets a pseudo tilt offset based on the floor heights in front and behind mario.
        if (gPuppyCam.flags & PUPPYCAM_BEHAVIOUR_HEIGHT_HELPER)
        {
            puppycam_view_height_offset();
        }
        else
        {
            gPuppyCam.posHeight[0] = 0;
            gPuppyCam.posHeight[1] = 0;
        }
    }
    else
    {
        puppycam_reset_values();
    }
}

//Support for most vanilla camera actions
static void puppycam_vanilla_actions(void)
{
    //Adds support for wing mario tower
    if (gMarioState->floor && gMarioState->floor->type == SURFACE_LOOK_UP_WARP
        && save_file_get_total_star_count(gCurrSaveFileNum - 1, COURSE_MIN - 1, COURSE_MAX - 1) >= 10) {
        if (gPuppyCam.pitch > 0x6000 && gMarioState->forwardVel == 0
            && (gMarioState->action & ACT_GROUP_MASK) != ACT_GROUP_CUTSCENE) {
            level_trigger_warp(gMarioState, WARP_OP_UNKNOWN_01);
        }
    }

    if (gSecondCameraFocus != NULL) {
        //Adds support for Bowser second camera focus
        if (gCurrLevelNum == LEVEL_BOWSER_1 || gCurrLevelNum == LEVEL_BOWSER_2 || gCurrLevelNum == LEVEL_BOWSER_3) {
            gPuppyCam.targetObj2 = gSecondCameraFocus;
        }

        //Adds support for BOB - WF bosses second camera focus
        if (gMarioState->floor && gMarioState->floor->type == SURFACE_BOSS_FIGHT_CAMERA) {
            gPuppyCam.targetObj2 = gSecondCameraFocus;
        } else {
            gPuppyCam.targetObj2 = NULL;
        }

    } else {
        gPuppyCam.targetObj2 = NULL;
    }
}

//void puppycam_shake(s16 x, s16 y, s16 z) {
//}

static void puppycam_analogue_stick(void)
{
    #ifdef TARGET_N64
    if (!gPuppyCam.options.analogue)
        return;

    //I make the X axis negative, so that the movement reflects the Cbuttons.
    gPuppyCam.stick2[0] = -gPlayer2Controller->rawStickX;
    gPuppyCam.stick2[1] = gPlayer2Controller->rawStickY;

    if (ABS(gPuppyCam.stick2[0]) < DEADZONE)
    {
        gPuppyCam.stick2[0] = 0;
        gPuppyCam.stickN[0] = 0;
    }
    if (ABS(gPuppyCam.stick2[1]) < DEADZONE)
    {
        gPuppyCam.stick2[1] = 0;
        gPuppyCam.stickN[1] = 0;
    }
    #endif
}

///This is the highest level of the basic steps that go into the code. Anything above is called from these following functions.

//The centrepiece behind the input side of PuppyCam. The C buttons branch off.
static void puppycam_input_core(void)
{
    UNUSED s32 stickMag = 0;

    puppycam_analogue_stick();
    gPuppyCam.moveFlagAdd = 0;

    if (gPuppyCam.options.analogue)
        stickMag = gPuppyCam.stick2[1]*1.25f;
    else
        stickMag = 100;

    //Decide which input for left and right C buttons to use based on behaviour type.
    if (gPuppyCam.flags & PUPPYCAM_BEHAVIOUR_INPUT_NORMAL)
        puppycam_input_hold();
    if (gPuppyCam.flags & PUPPYCAM_BEHAVIOUR_INPUT_8DIR || gPuppyCam.flags & PUPPYCAM_BEHAVIOUR_INPUT_4DIR)
        puppycam_input_press();

#ifdef MOUSE_ACTIONS
    puppycam_input_mouse();
#endif
}

//Calculates the base position the camera should be, before any modification.
static void puppycam_projection(void)
{
    Vec3s targetPos, targetPos2, targetPos3;
    s16 pitchTotal;
    s32 panD = (gPuppyCam.flags & PUPPYCAM_BEHAVIOUR_PANSHIFT)/8192;

    //Extra behaviours that get tacked onto the projection. Will be completely ignored if there is no target object.
    puppycam_projection_behaviours();
    //These are what the base rotations aspire to be.
    gPuppyCam.pitch = CLAMP(gPuppyCam.pitch,0x1000,0x7000);
    gPuppyCam.pitchTarget = CLAMP(gPuppyCam.pitchTarget,0x1000,0x7000);
    //These are the base rotations going to be used.
    gPuppyCam.yaw = gPuppyCam.yawTarget - approach_f32_asymptotic((s16)(gPuppyCam.yawTarget - gPuppyCam.yaw), 0, 0.3335f);
    gPuppyCam.pitch = gPuppyCam.pitchTarget - approach_f32_asymptotic((s16)(gPuppyCam.pitchTarget - gPuppyCam.pitch), 0, 0.3335f);
    //This adds the pitch effect when underwater, which is capped so it doesn't get out of control. If you're not swimming, swimpitch is 0, so it's normal.
    pitchTotal = CLAMP(gPuppyCam.pitch+(gPuppyCam.swimPitch*10)+gPuppyCam.edgePitch, 800, 0x7800);

    if (gPuppyCam.targetObj)
    {
        vec3s_set(targetPos, gPuppyCam.targetObj->oPosX, gPuppyCam.targetObj->oPosY, gPuppyCam.targetObj->oPosZ);
        vec3s_copy(targetPos3, targetPos);
        if (gPuppyCam.targetObj2)
        {
            vec3s_set(targetPos2, gPuppyCam.targetObj2->oPosX, gPuppyCam.targetObj2->oPosY, gPuppyCam.targetObj2->oPosZ);
            targetPos3[0] = (s16)approach_f32_asymptotic(targetPos[0], targetPos2[0], 0.5f);
            targetPos3[1] = (s16)approach_f32_asymptotic(targetPos[1], targetPos2[1], 0.5f);
            targetPos3[2] = (s16)approach_f32_asymptotic(targetPos[2], targetPos2[2], 0.5f);
            gPuppyCam.targetDist[0] = approach_f32_asymptotic(gPuppyCam.targetDist[0],(ABS(LENCOS(sqrtf(((targetPos[0]-targetPos2[0])*(targetPos[0]-targetPos2[0]))+((targetPos[2]-targetPos2[2])*(targetPos[2]-targetPos2[2]))),
                            (s16)ABS(((gPuppyCam.yaw + 0x8000) % 0xFFFF - 0x8000) - (atan2s(targetPos[2]-targetPos2[2], targetPos[0]-targetPos2[0])) % 0xFFFF - 0x8000)+0x4000))), 0.03f);
        }
        else
        {
            gPuppyCam.targetDist[0] = approach_f32_asymptotic(gPuppyCam.targetDist[0], 0, 0.2f);
        }

        gPuppyCam.targetDist[1] = gPuppyCam.targetDist[0] + gPuppyCam.zoom+gPuppyCam.moveZoom;

        if (gPuppyCam.flags & PUPPYCAM_BEHAVIOUR_X_MOVEMENT)
            gPuppyCam.focus[0] = targetPos3[0] + gPuppyCam.shake[0] + (gPuppyCam.pan[0]*gPuppyCam.targetDist[1]/gPuppyCam.zoomPoints[2])*panD;
        if (gPuppyCam.flags & PUPPYCAM_BEHAVIOUR_Y_MOVEMENT)
            gPuppyCam.focus[1] = targetPos3[1] + gPuppyCam.shake[1] + (gPuppyCam.pan[1]*gPuppyCam.targetDist[1]/gPuppyCam.zoomPoints[2]) + gPuppyCam.povHeight - gPuppyCam.floorY[0] + gPuppyCam.posHeight[0] + (gPuppyCam.swimPitch/10);
        if (gPuppyCam.flags & PUPPYCAM_BEHAVIOUR_Z_MOVEMENT)
            gPuppyCam.focus[2] = targetPos3[2] + gPuppyCam.shake[2] + (gPuppyCam.pan[2]*gPuppyCam.targetDist[1]/gPuppyCam.zoomPoints[2])*panD;

        if (gPuppyCam.flags & PUPPYCAM_BEHAVIOUR_X_MOVEMENT)
            gPuppyCam.pos[0] = gPuppyCam.targetObj->oPosX + LENSIN(LENSIN(gPuppyCam.targetDist[1],pitchTotal),gPuppyCam.yaw) + gPuppyCam.shake[0];
        if (gPuppyCam.flags & PUPPYCAM_BEHAVIOUR_Y_MOVEMENT)
            gPuppyCam.pos[1] = gPuppyCam.targetObj->oPosY + gPuppyCam.povHeight + LENCOS(gPuppyCam.targetDist[1],pitchTotal) + gPuppyCam.shake[1] - gPuppyCam.floorY[1] + gPuppyCam.posHeight[1];
        if (gPuppyCam.flags & PUPPYCAM_BEHAVIOUR_Z_MOVEMENT)
            gPuppyCam.pos[2] = gPuppyCam.targetObj->oPosZ + LENCOS(LENSIN(gPuppyCam.targetDist[1],pitchTotal),gPuppyCam.yaw) + gPuppyCam.shake[2];
    }

    puppycam_vanilla_actions();
}

//Calls any scripts to affect the camera, if applicable.
static void puppycam_script(void)
{
    u16 i = 0;
    struct sPuppyVolume volume;
    void (*func)();

    if (gPuppyVolumeCount == 0 || !gPuppyCam.targetObj)
        return;

    for (i = 0; i < gPuppyVolumeCount; i++)
    {
        if (puppycam_check_volume_bounds(&volume, i))
        {
            //First applies pos and focus, for the most basic of volumes.
            if (volume.angles != NULL)
            {
                if (volume.angles->pos[0] != PUPPY_NULL)
                    gPuppyCam.pos[0] = volume.angles->pos[0];
                if (volume.angles->pos[1] != PUPPY_NULL)
                    gPuppyCam.pos[1] = volume.angles->pos[1];
                if (volume.angles->pos[2] != PUPPY_NULL)
                    gPuppyCam.pos[2] = volume.angles->pos[2];

                if (volume.angles->focus[0] != PUPPY_NULL)
                    gPuppyCam.focus[0] = volume.angles->focus[0];
                if (volume.angles->focus[1] != PUPPY_NULL)
                    gPuppyCam.focus[1] = volume.angles->focus[1];
                if (volume.angles->focus[2] != PUPPY_NULL)
                    gPuppyCam.focus[2] = volume.angles->focus[2];

                if (volume.angles->yaw != PUPPY_NULL)
                {
                    gPuppyCam.yawTarget = volume.angles->yaw;
                    gPuppyCam.yaw = volume.angles->yaw;

                    gPuppyCam.flags &= ~PUPPYCAM_BEHAVIOUR_YAW_ROTATION;
                }

                if (volume.angles->pitch != PUPPY_NULL)
                {
                    gPuppyCam.pitchTarget = volume.angles->pitch;
                    gPuppyCam.pitch = volume.angles->pitch;

                    gPuppyCam.flags &= ~PUPPYCAM_BEHAVIOUR_PITCH_ROTATION;
                }

                if (volume.angles->zoom != PUPPY_NULL)
                {
                    gPuppyCam.zoomTarget = volume.angles->zoom;
                    gPuppyCam.zoom = gPuppyCam.zoomTarget;

                    gPuppyCam.flags &= ~PUPPYCAM_BEHAVIOUR_ZOOM_CHANGE;
                }
            }

            //Adds and removes behaviour flags, as set.
            if (volume.flagsRemove)
                gPuppyCam.flags &= ~volume.flagsRemove;
            if (volume.flagsAdd)
                gPuppyCam.flags |= volume.flagsAdd;
            if (volume.flagPersistance == PUPPYCAM_BEHAVIOUR_PERMANENT)
            {
                //Adds and removes behaviour flags, as set.
                if (volume.flagsRemove)
                    gPuppyCam.intendedFlags &= ~volume.flagsRemove;
                if (volume.flagsAdd)
                    gPuppyCam.intendedFlags |= volume.flagsAdd;
            }

            //Last and probably least, check if there's a function attached, and call it, if so.
            if (volume.func)
            {
                func = volume.func;
                (func)();
            }
        }
    }
}

void puppycam_script_clear(void)
{
    s32 i = 0;
    for (i = 0; i < gPuppyVolumeCount; i++)
    {
        mem_pool_free(gPuppyMemoryPool, sPuppyVolumeStack[i]);
    }
    gPuppyVolumeCount = 0;
}

//Handles collision detection using ray casting.
static void puppycam_collision(void)
{
    struct Surface *surf[2];
    Vec3f camdir[2];
    Vec3f hitpos[2];
    Vec3f target[2];
    s16 pitchTotal = CLAMP(gPuppyCam.pitch+(gPuppyCam.swimPitch*10), 800, 0x7800);
    s32 dist[2];

    if (gPuppyCam.targetObj == NULL)
        return;

    //The ray, starting from the top
    target[0][0] = gPuppyCam.targetObj->oPosX;
    target[0][1] = gPuppyCam.targetObj->oPosY + (gPuppyCam.povHeight) - CLAMP(gPuppyCam.targetObj->oPosY - gPuppyCam.targetFloorHeight, 0, 300);
    target[0][2] = gPuppyCam.targetObj->oPosZ;
    //The ray, starting from the bottom
    target[1][0] = gPuppyCam.targetObj->oPosX;
    target[1][1] = gPuppyCam.targetObj->oPosY + (gPuppyCam.povHeight *0.4f);
    target[1][2] = gPuppyCam.targetObj->oPosZ;

    camdir[0][0] = LENSIN(LENSIN(gPuppyCam.zoomTarget,pitchTotal),gPuppyCam.yaw) + gPuppyCam.shake[0];
    camdir[0][1] = LENCOS(gPuppyCam.zoomTarget,pitchTotal) + gPuppyCam.shake[1];// + gPuppyCam.posHeight[1];
    camdir[0][2] = LENCOS(LENSIN(gPuppyCam.zoomTarget,pitchTotal),gPuppyCam.yaw) + gPuppyCam.shake[2];

    camdir[1][0] = camdir[0][0];
    camdir[1][1] = camdir[0][1];
    camdir[1][2] = camdir[0][2];

    find_surface_on_ray(target[0], camdir[0], &surf[0], hitpos[0]);
    find_surface_on_ray(target[1], camdir[1], &surf[1], hitpos[1]);
    resolve_and_return_wall_collisions(hitpos[0], 0.0f, 50.0f);
    resolve_and_return_wall_collisions(hitpos[1], 0.0f, 50.0f);
    dist[0] = ((target[0][0] - hitpos[0][0]) * (target[0][0] - hitpos[0][0]) + (target[0][1] - hitpos[0][1]) * (target[0][1] - hitpos[0][1]) + (target[0][2] - hitpos[0][2]) * (target[0][2] - hitpos[0][2]));
    dist[1] = ((target[1][0] - hitpos[1][0]) * (target[1][0] - hitpos[1][0]) + (target[1][1] - hitpos[1][1]) * (target[1][1] - hitpos[1][1]) + (target[1][2] - hitpos[1][2]) * (target[1][2] - hitpos[1][2]));

    gPuppyCam.collisionDistance = gPuppyCam.zoomTarget;

    if (surf[0] && surf[1])
    {
        gPuppyCam.collisionDistance = sqrtf(MAX(dist[0], dist[1]));
        if (gPuppyCam.zoom > gPuppyCam.collisionDistance)
        {
            gPuppyCam.zoom = MIN(gPuppyCam.collisionDistance, gPuppyCam.zoomTarget);
            if (gPuppyCam.zoom - gPuppyCam.zoomTarget < 5)
            {
                if (dist[0] >= dist[1])
                {
                    gPuppyCam.pos[0] = hitpos[0][0];
                    gPuppyCam.pos[1] = hitpos[0][1];
                    gPuppyCam.pos[2] = hitpos[0][2];
                }
                else
                {
                    gPuppyCam.pos[0] = hitpos[1][0];
                    gPuppyCam.pos[1] = hitpos[1][1] + (gPuppyCam.povHeight*0.6f);
                    gPuppyCam.pos[2] = hitpos[1][2];
                }
            }
        }
    }

    #define START_DIST 500
    #define END_DIST 250
    gPuppyCam.opacity = CLAMP((f32)(((gPuppyCam.zoom-END_DIST)/255.0f)*(START_DIST-END_DIST)), 0, 255);
}

extern Vec3f sOldPosition;
extern Vec3f sOldFocus;
extern struct PlayerGeometry sMarioGeometry;

//Applies the PuppyCam values to the actual game's camera, giving the final product.
static void puppycam_apply(void)
{
    vec3f_set(gLakituState.pos, (f32)gPuppyCam.pos[0], (f32)gPuppyCam.pos[1], (f32)gPuppyCam.pos[2]);
    vec3f_set(gLakituState.goalPos, (f32)gPuppyCam.pos[0], (f32)gPuppyCam.pos[1], (f32)gPuppyCam.pos[2]);
    vec3f_set(gLakituState.curPos, (f32)gPuppyCam.pos[0], (f32)gPuppyCam.pos[1], (f32)gPuppyCam.pos[2]);
    vec3f_set(gCamera->pos, (f32)gPuppyCam.pos[0], (f32)gPuppyCam.pos[1], (f32)gPuppyCam.pos[2]);
    vec3f_set(sOldPosition, (f32)gPuppyCam.pos[0], (f32)gPuppyCam.pos[1], (f32)gPuppyCam.pos[2]);

    vec3f_set(gLakituState.focus, (f32)gPuppyCam.focus[0], (f32)gPuppyCam.focus[1], (f32)gPuppyCam.focus[2]);
    vec3f_set(gLakituState.goalFocus, (f32)gPuppyCam.focus[0], (f32)gPuppyCam.focus[1], (f32)gPuppyCam.focus[2]);
    vec3f_set(gLakituState.curFocus, (f32)gPuppyCam.focus[0], (f32)gPuppyCam.focus[1], (f32)gPuppyCam.focus[2]);
    vec3f_set(gCamera->focus, (f32)gPuppyCam.focus[0], (f32)gPuppyCam.focus[1], (f32)gPuppyCam.focus[2]);
    vec3f_set(sOldFocus, (f32)gPuppyCam.focus[0], (f32)gPuppyCam.focus[1], (f32)gPuppyCam.focus[2]);

    gCamera->yaw = gPuppyCam.yaw;
    gCamera->nextYaw = gPuppyCam.yaw;

    gLakituState.yaw = gPuppyCam.yaw;
    gLakituState.nextYaw = gPuppyCam.yaw;
    gLakituState.oldYaw = gPuppyCam.yaw;

    gLakituState.mode = gCamera->mode;
    gLakituState.defMode = gCamera->defMode;

    gLakituState.roll = approach_s32(gLakituState.roll, 0, 0x80, 0x80);

    if (gMarioState->floor != NULL) {
        sMarioGeometry.currFloor = gMarioState->floor;
        sMarioGeometry.currFloorHeight = gMarioState->floorHeight;
        sMarioGeometry.currFloorType = gMarioState->floor->type;
    }

    if (gMarioState->ceil != NULL) {
        sMarioGeometry.currCeil = gMarioState->ceil;
        sMarioGeometry.currCeilHeight = gMarioState->ceilHeight;
        sMarioGeometry.currCeilType = gMarioState->ceil->type;
    }
}

extern Texture texture_hud_char_puppycam[];
extern void render_hud_tex_lut(s32 x, s32 y, u8 *texture);
extern void render_hud_small_tex_lut(s32 x, s32 y, u8 *texture);

//Puppycam HUD logic
void puppycam_hud(void) {
    s32 x, y;
    u8 *(*ogCameraLUT)[6];
    s16 ogCamStatus = update_camera_hud_status(gCamera);

    ogCameraLUT = segmented_to_virtual(&main_hud_camera_lut);
    x = GFX_DIMENSIONS_RECT_FROM_RIGHT_EDGE(54);
    y = 205;

    gSPDisplayList(gDisplayListHead++, dl_hud_img_begin);

    // Main HUD Icons
    render_hud_tex_lut(x, y, (*ogCameraLUT)[GLYPH_CAM_CAMERA]);

    if (ogCamStatus & CAM_STATUS_FIXED) {
        render_hud_tex_lut(x + 16, y, (*ogCameraLUT)[GLYPH_CAM_FIXED]);
    } else {
        render_hud_tex_lut(x + 16, y, texture_hud_char_puppycam);
    }

    // Arrow Icons
    if (gPuppyCam.options.inputType != PUPPYCAM_INPUT_TYPE_CLASSIC) {
        switch (gPuppyCam.zoomSet) {
            case 0:
                render_hud_small_tex_lut(x + 4, y - 8, (*ogCameraLUT)[GLYPH_CAM_ARROW_UP]);
                break;
            case 1:
                break;
            case 2:
                render_hud_small_tex_lut(x + 4, y + 16, (*ogCameraLUT)[GLYPH_CAM_ARROW_DOWN]);
                break;
        }
    } else {
        switch (gPuppyCam.mode3Flags & PUPPYCAM_MODE3_GROUP) {
            case PUPPYCAM_MODE3_ZOOMED_IN:
                render_hud_small_tex_lut(x + 4, y - 8, (*ogCameraLUT)[GLYPH_CAM_ARROW_UP]);
                break;
            case PUPPYCAM_MODE3_ZOOMED_MED:
                break;
            case PUPPYCAM_MODE3_ZOOMED_OUT:
                render_hud_small_tex_lut(x + 4, y + 16, (*ogCameraLUT)[GLYPH_CAM_ARROW_DOWN]);
                break;
        }
    }

    gSPDisplayList(gDisplayListHead++, dl_hud_img_end);
}



//The basic loop sequence, which is called outside.
void puppycam_loop(void)
{
    if (!gPuppyCam.cutscene && sDelayedWarpOp == 0)
    {
        //Sets this before going through any possible modifications.
        gPuppyCam.flags = gPuppyCam.intendedFlags;
        puppycam_input_core();
        puppycam_projection();
        puppycam_script();
        if (gPuppyCam.flags & PUPPYCAM_BEHAVIOUR_COLLISION)
            puppycam_collision();
        else
            gPuppyCam.opacity = 255;
    }
    else
    if (gPuppyCam.cutscene)
    {
        gPuppyCam.opacity = 255;
        puppycam_process_cutscene();
    }

    puppycam_apply();
}

#endif
