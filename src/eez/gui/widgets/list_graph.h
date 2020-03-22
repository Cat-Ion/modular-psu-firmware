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

namespace eez {
namespace gui {

struct ListGraphWidget {
    int16_t dwellData;
    int16_t y1Data;
    uint16_t y1Style;
    int16_t y2Data;
    uint16_t y2Style;
    int16_t cursorData;
    uint16_t cursorStyle;
};

struct ListGraphWidgetState {
    WidgetState genericState;
    Value cursorData;
};

void ListGraphWidget_draw(const WidgetCursor &widgetCursor);
void ListGraphWidget_onTouch(const WidgetCursor &widgetCursor, Event &touchEvent);

} // namespace gui
} // namespace eez