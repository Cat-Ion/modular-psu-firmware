/*
 * EEZ Modular Firmware
 * Copyright (C) 2015-present, Envox d.o.o.
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.

 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.

 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#pragma once

#if OPTION_GUI_THREAD
#include <cmsis_os.h>
#endif

#include <eez/gui/assets.h>
#include <eez/gui/page.h>

#include <eez/modules/mcu/display.h>

enum {
    FIRST_INTERNAL_PAGE_ID = 32000,
    INTERNAL_PAGE_ID_SELECT_FROM_ENUM,
    INTERNAL_PAGE_ID_TOAST_MESSAGE,
    INTERNAL_PAGE_ID_MENU_WITH_BUTTONS
};

enum InternalActionsEnum {
    FIRST_INTERNAL_ACTION_ID = 32000,
    ACTION_ID_INTERNAL_SELECT_ENUM_ITEM,
    ACTION_ID_INTERNAL_DIALOG_CLOSE,
    ACTION_ID_INTERNAL_TOAST_ACTION,
    ACTION_ID_INTERNAL_TOAST_ACTION_WITHOUT_PARAM,
    ACTION_ID_INTERNAL_MENU_WITH_BUTTONS
};

namespace eez {

namespace gui {

////////////////////////////////////////////////////////////////////////////////

#if OPTION_GUI_THREAD

void startThread();

extern osThreadId g_guiTaskHandle;
extern osMessageQId g_guiMessageQueueId;

enum {
    GUI_QUEUE_MESSAGE_TYPE_SHOW_PAGE = 1,
    GUI_QUEUE_MESSAGE_TYPE_PUSH_PAGE,

    GUI_QUEUE_MESSAGE_MOUSE_X_MOVE,
    GUI_QUEUE_MESSAGE_MOUSE_Y_MOVE,
    GUI_QUEUE_MESSAGE_MOUSE_BUTTON_DOWN,
    GUI_QUEUE_MESSAGE_MOUSE_BUTTON_UP,
    GUI_QUEUE_MESSAGE_MOUSE_DISCONNECTED,

    GUI_QUEUE_MESSAGE_REFRESH_SCREEN,
    
    GUI_QUEUE_MESSAGE_KEY_DOWN
};

void sendMessageToGuiThread(uint8_t messageType, uint32_t messageParam = 0, uint32_t timeoutMillisec = osWaitForever);

void onGuiQueueMessageHook(uint8_t type, int16_t param);

#endif

////////////////////////////////////////////////////////////////////////////////

extern bool g_isBlinkTime;

////////////////////////////////////////////////////////////////////////////////

void stateManagmentHook();
WidgetCursor &getFoundWidgetAtDown();
void setFoundWidgetAtDown(WidgetCursor &widgetCursor);
void clearFoundWidgetAtDown();
bool isActiveWidget(const WidgetCursor &widgetCursor);
bool isFocusWidget(const WidgetCursor &widgetCursor);
void refreshScreen();
bool isPageInternal(int pageId);
void executeAction(int actionId);

int16_t getAppContextId(AppContext *pAppContext);
AppContext *getAppContextFromId(int16_t id);

////////////////////////////////////////////////////////////////////////////////

enum Buffer {
    BUFFER_OLD,
    BUFFER_NEW,
    BUFFER_SOLID_COLOR
};

struct AnimationState {
    bool enabled;
    uint32_t startTime;
    float duration;
    Buffer startBuffer;
    void (*callback)(float t, void *bufferOld, void *bufferNew, void *bufferDst);
    float (*easingRects)(float x, float x1, float y1, float x2, float y2);
    float (*easingOpacity)(float x, float x1, float y1, float x2, float y2);
};

enum Opacity {
    OPACITY_SOLID,
    OPACITY_FADE_IN,
    OPACITY_FADE_OUT
};

enum Position {
    POSITION_TOP_LEFT,
    POSITION_TOP,
    POSITION_TOP_RIGHT,
    POSITION_LEFT,
    POSITION_CENTER,
    POSITION_RIGHT,
    POSITION_BOTTOM_LEFT,
    POSITION_BOTTOM,
    POSITION_BOTTOM_RIGHT
};

struct AnimRect {
    Buffer buffer;
    Rect srcRect;
    Rect dstRect;
    uint16_t color;
    Opacity opacity;
    Position position;
};

extern AnimationState g_animationState;

#define MAX_ANIM_RECTS 10

extern AnimRect g_animRects[MAX_ANIM_RECTS];

void animateOpen(const Rect &srcRect, const Rect &dstRect);
void animateClose(const Rect &srcRect, const Rect &dstRect);
void animateRects(AppContext *appContext, Buffer startBuffer, int numRects, float duration = -1);

float getDefaultAnimationDurationHook();

void executeExternalActionHook(int32_t actionId);
void externalDataHook(int16_t id, DataOperationEnum operation, Cursor cursor, Value &value);

////////////////////////////////////////////////////////////////////////////////

int getCurrentStateBufferIndex();
uint32_t getCurrentStateBufferSize(const WidgetCursor &widgetCursor);

Page *getPageFromIdHook(int pageId);

void executeInternalActionHook(int actionId);

bool activePageHasBackdropHook();

} // namespace gui
} // namespace eez

#include <eez/gui/app_context.h>
#include <eez/gui/update.h>
#include <eez/gui/touch.h>
#include <eez/gui/overlay.h>
#include <eez/gui/font.h>
#include <eez/gui/draw.h>

#define DATA_OPERATION_FUNCTION(id, operation, cursor, value) (id >= 0 ? g_dataOperationsFunctions[id](operation, cursor, value) : externalDataHook(id, operation, cursor, value))
