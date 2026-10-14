// What the runtime needs from the platform around it.
//
// The runtime owns all project state (positions, costumes, effects, pen
// state...). The host owns pixels and audio: it draws, plays sounds, answers
// geometric questions (touching, bounds) and reports input. The method set
// mirrors the parts of scratch-render and the audio engine that scratch-vm
// uses, so the runtime ports line up one to one with scratch-vm's blocks.
//
// A runtime without a host behaves like scratch-vm without a renderer:
// nothing is drawn, geometric queries answer "no".
#pragma once

#include <cstdint>
#include <string>

namespace lithium {

struct Rect {
    double left = 0, right = 0, top = 0, bottom = 0;
};

// scratch-vm's StageLayering.
enum LayerGroup { kBackgroundLayer = 0, kVideoLayer = 1, kPenLayer = 2, kSpriteLayer = 3 };

class Host {
public:
    virtual ~Host() = default;

    // ---- drawables -------------------------------------------------------
    virtual int createDrawable(int layerGroup) = 0;
    virtual void destroyDrawable(int id, int layerGroup) = 0;
    virtual void updateDrawableSkinId(int id, int skinId) = 0;
    virtual void updateDrawablePosition(int id, double x, double y) = 0;
    virtual void updateDrawableDirectionScale(int id, double direction, double scaleX, double scaleY) = 0;
    virtual void updateDrawableVisible(int id, bool visible) = 0;
    virtual void updateDrawableEffect(int id, const char* effect, double value) = 0;
    // RenderWebGL.setDrawableOrder; returns the new order.
    virtual double setDrawableOrder(int id, double order, int layerGroup, bool relative, double min) = 0;

    // ---- geometry queries (all in Scratch stage coordinates) -------------
    virtual void getFencedPosition(int id, double x, double y, double* outX, double* outY) = 0;
    virtual void getCurrentSkinSize(int id, double* width, double* height) = 0;
    // false when the drawable has no bounds.
    virtual bool getBounds(int id, Rect* out) = 0;
    virtual bool drawableTouching(int id, double x, double y) = 0;
    virtual bool isTouchingDrawables(int id, const int* others, int count) = 0;
    virtual bool isTouchingColor(int id, const int rgb[3]) = 0;
    virtual bool colorIsTouchingColor(int id, const int target[3], const int mask[3]) = 0;
    // Topmost drawable under a point in canvas pixels, or -1.
    virtual int pick(double canvasX, double canvasY) = 0;

    // ---- pen ---------------------------------------------------------------
    virtual void penClear() = 0;
    virtual void penStamp(int drawableId) = 0;
    virtual void penPoint(const double color4f[4], double diameter, double x, double y) = 0;
    virtual void penLine(const double color4f[4], double diameter, double x0, double y0, double x1, double y1) = 0;

    // ---- speech bubbles, questions, sound ---------------------------------
    // type is "say" or "think"; empty text removes the bubble.
    virtual void setBubble(int targetUid, int drawableId, const char* type, const char* text) = 0;
    // Show a question to the user (null = hide the prompt).
    virtual void ask(const char* question) = 0;
    // Plays a sound. When requestId != 0 the host reports completion (or
    // being stopped) with Runtime::soundFinished(requestId).
    virtual void playSound(int targetUid, int soundIndex, int requestId) = 0;
    virtual void stopSound(int targetUid, int soundIndex) = 0;
    virtual void stopAllSounds(int targetUid) = 0;
    virtual void setSoundEffects(int targetUid, double volume, double pitch, double pan) = 0;
    virtual void targetRemoved(int targetUid) = 0;
};

}  // namespace lithium
