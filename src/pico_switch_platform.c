#include <stdio.h>
#include <string.h>

#include <pico/cyw43_arch.h>
#include <pico/multicore.h>
#include <pico/async_context.h>
#include <uni.h>
#include "pico/time.h"

#include "sdkconfig.h"
#include "uni_hid_device.h"
#include "uni_log.h"
#include "usb.h"
#include "report.h"
#include "SwitchDescriptors.h"
#include "KeyboardKeys.h"

// Sanity check
#ifndef CONFIG_BLUEPAD32_PLATFORM_CUSTOM
#error "Pico W must use BLUEPAD32_PLATFORM_CUSTOM"
#endif

#define AXIS_DEADZONE 0xa
#define JOYSTICK_CENTER 0x80
#define MOUSE_SENSITIVITY 5
#define MOUSE_IDLE_TIMEOUT_MS 40
static uint32_t last_mouse_move_time_ms = 0;

// Declarations
static void trigger_event_on_gamepad(uni_hid_device_t *d);
SwitchOutReport report[CONFIG_BLUEPAD32_MAX_DEVICES];
SwitchIdxOutReport idx_r;
uint8_t connected_controllers;

typedef struct {
    bool has_keyboard;
    bool has_mouse;
    uni_keyboard_t keyboard;
    uni_mouse_t mouse;
} CombinedControllerState;

static CombinedControllerState combined_states[CONFIG_BLUEPAD32_MAX_DEVICES];

// Helper functions
static void
empty_gamepad_report(SwitchOutReport *gamepad)
{
	gamepad->buttons = 0;
	gamepad->hat = SWITCH_HAT_NOTHING;
	gamepad->lx = SWITCH_JOYSTICK_MID;
	gamepad->ly = SWITCH_JOYSTICK_MID;
	gamepad->rx = SWITCH_JOYSTICK_MID;
	gamepad->ry = SWITCH_JOYSTICK_MID;
}

uint8_t
convert_to_switch_axis(int32_t bluepadAxis)
{
	// bluepad32 reports from -512 to 511 as int32_t
	// switch reports from 0 to 255 as uint8_t

	bluepadAxis += 513;  // now max possible is 1024
	bluepadAxis /= 4;    // now max possible is 255

	if (bluepadAxis < SWITCH_JOYSTICK_MIN)
		bluepadAxis = 0;
	else if ((bluepadAxis > (SWITCH_JOYSTICK_MID - AXIS_DEADZONE)) &&
	         (bluepadAxis < (SWITCH_JOYSTICK_MID + AXIS_DEADZONE))) {
		bluepadAxis = SWITCH_JOYSTICK_MID;
	} else if (bluepadAxis > SWITCH_JOYSTICK_MAX)
		bluepadAxis = SWITCH_JOYSTICK_MAX;

	return (uint8_t) bluepadAxis;
}

// Clamp between 0 and 255
static uint8_t clamp_stick_value(int val) 
{
    if (val < 0) return 0;
    if (val > 255) return 255;
    return (uint8_t)val;
}

static void fill_gamepad_report_from_keyboard(int idx, const uni_keyboard_t* gp) 
{
    
	if ((gp->modifiers & UNI_KEYBOARD_MODIFIER_LEFT_SHIFT)) {
        report[idx].buttons |= SWITCH_MASK_L3;
    }

	if ((gp->modifiers & UNI_KEYBOARD_MODIFIER_LEFT_CONTROL)) {
        report[idx].buttons |= SWITCH_MASK_R3;
    }

    for (int i = 0; i < UNI_KEYBOARD_PRESSED_KEYS_MAX; i++) {
        uint8_t key = gp->pressed_keys[i];
        switch (key) {

			// A Button
            case KEY_Q:
                report[idx].buttons |= SWITCH_MASK_A;
                break;

			// B Button
			case KEY_SPACE:
                report[idx].buttons |= SWITCH_MASK_B;
                break;

			// X Button
            case KEY_R:
                report[idx].buttons |= SWITCH_MASK_X;
                break;

			// Y Button
			case KEY_E:
				report[idx].buttons |= SWITCH_MASK_Y;
                break;
			
			// Dpad Down
            case KEY_B:
                report[idx].hat = SWITCH_HAT_DOWN;
                break;

			//Dpad Up
			case KEY_F:
                report[idx].hat = SWITCH_HAT_UP;
                break;
			
			//Dpad Right
			case KEY_I:
                report[idx].hat = SWITCH_HAT_RIGHT;
                break;

			//Minus Button
			case KEY_TAB:
                report[idx].buttons |= SWITCH_MASK_MINUS;
                break;
			
			//Plus Button
			case KEY_ESC:
                report[idx].buttons |= SWITCH_MASK_PLUS;
                break;
			
			//Home Button
			case KEY_H:
                report[idx].buttons |= SWITCH_MASK_HOME;
                break;
			
			//Capture Button
			case KEY_C:
                report[idx].buttons |= SWITCH_MASK_CAPTURE;
                break;

			//Left Joystick movement
            case KEY_W:
                report[idx].ly = 0x00; // up
                break;

            case KEY_S:
                report[idx].ly = 0xFF; // down
                break;

            case KEY_A:
                report[idx].lx = 0x00; // left
                break;

            case KEY_D:
                report[idx].lx = 0xFF; // right
                break;

            default:
                break;
        }
    }
}

static void fill_gamepad_report_from_mouse(int idx, const uni_mouse_t* mouse) 
{   
	absolute_time_t now = get_absolute_time();
    uint32_t now_ms = to_ms_since_boot(now);

	//right click
    if (mouse->buttons & MOUSE_BUTTON_RIGHT) 
	{
        report[idx].buttons |= SWITCH_MASK_ZL;
	} 
	else 
	{
		report[idx].buttons &= ~SWITCH_MASK_ZL; 
    }

	//left click
    if (mouse->buttons & MOUSE_BUTTON_LEFT) 
	{
        report[idx].buttons |= SWITCH_MASK_ZR;
    } 
	else 
	{
		report[idx].buttons &= ~SWITCH_MASK_ZR; 
	}

	//middle click
	if (mouse->buttons & MOUSE_BUTTON_MIDDLE) 
	{
        report[idx].hat = SWITCH_HAT_LEFT;
    }

	//scroll wheel
    if (mouse->scroll_wheel > 0) //up
	{
		report[idx].buttons |= SWITCH_MASK_L;
		((uni_mouse_t*)mouse)->scroll_wheel = 0;
	}

	if (mouse->scroll_wheel < 0) //down
	{
		report[idx].buttons |= SWITCH_MASK_R;
		((uni_mouse_t*)mouse)->scroll_wheel = 0;
	}

	//mouse movement
    if (mouse->delta_x != 0 || mouse->delta_y != 0) {
        last_mouse_move_time_ms = now_ms;
        int rx = JOYSTICK_CENTER + (mouse->delta_x * MOUSE_SENSITIVITY);
        int ry = JOYSTICK_CENTER + (mouse->delta_y * MOUSE_SENSITIVITY);

        report[idx].rx = clamp_stick_value(rx);
        report[idx].ry = clamp_stick_value(ry);

    } 
	else 
	{
        if ((now_ms - last_mouse_move_time_ms) < MOUSE_IDLE_TIMEOUT_MS) {} 
		else 
		{
            report[idx].rx = JOYSTICK_CENTER;
            report[idx].ry = JOYSTICK_CENTER;
			
		}
    }
}

static void
fill_gamepad_report(int idx, uni_controller_t* ctl)
{
	empty_gamepad_report(&report[idx]);

	//Keyboard logic
	if (ctl->klass == UNI_CONTROLLER_CLASS_KEYBOARD) {

        uni_keyboard_t* gp = &ctl->keyboard;
		// face buttons
		if ((gp->modifiers & UNI_KEYBOARD_MODIFIER_LEFT_SHIFT)) {

			report[idx].buttons |= SWITCH_MASK_L3;
		}

		for (int i = 0; i < UNI_KEYBOARD_PRESSED_KEYS_MAX; i++) {

			if (gp->pressed_keys[i] == KEY_E) {
				report[idx].buttons |= SWITCH_MASK_Y;
				break;
			}

			if (gp->pressed_keys[i] == KEY_R) {
				report[idx].buttons |= SWITCH_MASK_Y;
				break;
			}

			if (gp->pressed_keys[i] == KEY_SPACE) {
				report[idx].buttons |= SWITCH_MASK_B;
				break;
			}

			if (gp->pressed_keys[i] == KEY_B) {
				report[idx].hat = SWITCH_HAT_DOWN;
				break;
			}

			if (gp->pressed_keys[i] == KEY_Q) {
				report[idx].buttons |= SWITCH_MASK_A;
				break;
			}

			switch (gp->pressed_keys[i]) 
			{
				case KEY_W: report[idx].ly = 0x00; break;  // up
				case KEY_S: report[idx].ly = 0xFF; break;  // down
				case KEY_A: report[idx].lx = 0x00; break;  // left
				case KEY_D: report[idx].lx = 0xFF; break;  // right
			}


		}
	}

	//Mouse logic
    if (ctl->klass == UNI_CONTROLLER_CLASS_MOUSE) {

        uni_mouse_t* mouse = &ctl->mouse;
		absolute_time_t now = get_absolute_time();
		uint32_t now_ms = to_ms_since_boot(now);

        if (mouse->buttons & MOUSE_BUTTON_RIGHT)
		{
			report[idx].buttons |= SWITCH_MASK_ZL;
		}

		if (mouse->buttons & MOUSE_BUTTON_LEFT)
		{
			report[idx].buttons |= SWITCH_MASK_ZR;
		}

		if (mouse->scroll_wheel > 0)
		{
			report[idx].buttons |= SWITCH_MASK_R;
		}
        
		if (mouse->scroll_wheel < 0)
		{
			report[idx].buttons |= SWITCH_MASK_L;
		}
		
		/*
		int rx = JOYSTICK_CENTER + (mouse->delta_x * MOUSE_SENSITIVITY);
		int ry = JOYSTICK_CENTER + (mouse->delta_y * MOUSE_SENSITIVITY);

		report[idx].rx = clamp_stick_value(rx);
		report[idx].ry = clamp_stick_value(ry);
		*/
		
		if (mouse->delta_x != 0 || mouse->delta_y != 0) {
			last_mouse_move_time_ms = now_ms;

			int rx = JOYSTICK_CENTER + (mouse->delta_x * MOUSE_SENSITIVITY);
			int ry = JOYSTICK_CENTER + (mouse->delta_y * MOUSE_SENSITIVITY);

			report[idx].rx = clamp_stick_value(rx);
			report[idx].ry = clamp_stick_value(ry);
		}
		else {
			if ((now_ms - last_mouse_move_time_ms) < MOUSE_IDLE_TIMEOUT_MS) {
				// Keep stick at last value — don’t snap to center yet
			} else {
				// Reset stick to center after timeout
				report[idx].rx = JOYSTICK_CENTER;
				report[idx].ry = JOYSTICK_CENTER;
			}
		}
            
    }
	/*
	if ((gp->modifiers & KEY_W)) {
		report[idx].buttons |= SWITCH_MASK_B;
	}
	
	if ((gp->buttons & BUTTON_X)) {
		report[idx].buttons |= SWITCH_MASK_X;
	}
	if ((gp->buttons & BUTTON_Y)) {
		report[idx].buttons |= SWITCH_MASK_Y;
	}

	// shoulder buttons
	if ((gp->buttons & BUTTON_SHOULDER_L)) {
		report[idx].buttons |= SWITCH_MASK_L;
	}
	if ((gp->buttons & BUTTON_SHOULDER_R)) {
		report[idx].buttons |= SWITCH_MASK_R;
	}

	// dpad
	switch (gp->dpad) {
	case DPAD_UP:
		report[idx].hat = SWITCH_HAT_UP;
		break;
	case DPAD_DOWN:
		report[idx].hat = SWITCH_HAT_DOWN;
		break;
	case DPAD_LEFT:
		report[idx].hat = SWITCH_HAT_LEFT;
		break;
	case DPAD_RIGHT:
		report[idx].hat = SWITCH_HAT_RIGHT;
		break;
	case DPAD_UP | DPAD_RIGHT:
		report[idx].hat = SWITCH_HAT_UPRIGHT;
		break;
	case DPAD_DOWN | DPAD_RIGHT:
		report[idx].hat = SWITCH_HAT_DOWNRIGHT;
		break;
	case DPAD_DOWN | DPAD_LEFT:
		report[idx].hat = SWITCH_HAT_DOWNLEFT;
		break;
	case DPAD_UP | DPAD_LEFT:
		report[idx].hat = SWITCH_HAT_UPLEFT;
		break;
	default:
		report[idx].hat = SWITCH_HAT_NOTHING;
		break;
	}
	*/
	/*
	// sticks
	report[idx].lx = convert_to_switch_axis(gp->axis_x);
	report[idx].ly = convert_to_switch_axis(gp->axis_y);
	report[idx].rx = convert_to_switch_axis(gp->axis_rx);
	report[idx].ry = convert_to_switch_axis(gp->axis_ry);
	if ((gp->buttons & BUTTON_THUMB_L))
		report[idx].buttons |= SWITCH_MASK_L3;
	if ((gp->buttons & BUTTON_THUMB_R))
		report[idx].buttons |= SWITCH_MASK_R3;

	
	// triggers
	if (gp->brake)
		report[idx].buttons |= SWITCH_MASK_ZL;
	if (gp->throttle)
		report[idx].buttons |= SWITCH_MASK_ZR;

	// misc buttons
	if (gp->misc_buttons & MISC_BUTTON_SYSTEM)
		report[idx].buttons |= SWITCH_MASK_HOME;
	if (gp->misc_buttons & MISC_BUTTON_CAPTURE)
		report[idx].buttons |= SWITCH_MASK_CAPTURE;
	if (gp->misc_buttons & MISC_BUTTON_BACK)
		report[idx].buttons |= SWITCH_MASK_MINUS;
	if (gp->misc_buttons & MISC_BUTTON_HOME)
		report[idx].buttons |= SWITCH_MASK_PLUS;
	*/
}

static void
set_led_status() {
	if (connected_controllers == 0)
		cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 0);
	else
		cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 1);
}

//
// Platform Overrides
//
static void pico_switch_platform_init(int argc, const char** argv) 
{
    ARG_UNUSED(argc);
    ARG_UNUSED(argv);

    logi("my_platform: init()\n");

	connected_controllers = 0;

	uni_gamepad_mappings_t mappings = GAMEPAD_DEFAULT_MAPPINGS;

	// remaps
	mappings.button_b = UNI_GAMEPAD_MAPPINGS_BUTTON_A;
	mappings.button_a = UNI_GAMEPAD_MAPPINGS_BUTTON_B;
	mappings.button_y = UNI_GAMEPAD_MAPPINGS_BUTTON_X;
	mappings.button_x = UNI_GAMEPAD_MAPPINGS_BUTTON_Y;

	uni_gamepad_set_mappings(&mappings);

	idx_r.idx = 0;
	idx_r.report.buttons = 0;
	idx_r.report.hat = SWITCH_HAT_NOTHING;
	idx_r.report.lx = 0;
	idx_r.report.ly = 0;
	idx_r.report.rx = 0;
	idx_r.report.ry = 0;
	set_global_gamepad_report(&idx_r);

}

static void pico_switch_platform_on_init_complete(void) {
    logi("my_platform: on_init_complete()\n");

    // Safe to call "unsafe" functions since they are called from BT thread

    // Start scanning
    uni_bt_enable_new_connections_unsafe(true);

    // Based on runtime condition you can delete or list the stored BT keys.
    if (1)
        uni_bt_del_keys_unsafe();
    else
        uni_bt_list_keys_unsafe();

    // Turn off LED once init is done.
    cyw43_arch_gpio_put(CYW43_WL_GPIO_LED_PIN, 0);

	logi("BLUEPAD: ready to fill reports");
	multicore_fifo_push_blocking(0); // signal other core to start reading
}

static void pico_switch_platform_on_device_connected(uni_hid_device_t* d) {
    logi("my_platform: device connected: %p\n", d);
}

static void pico_switch_platform_on_device_disconnected(uni_hid_device_t* d) {
    logi("my_platform: device disconnected: %p\n", d);
	// NOT WORKING?
	// This is complicated. uni_hid_device_get_idx_for_instance 
	// no longer gives us the index once the device is disconnected.
	// We assume in this case that a device disconnecting is in a state of no gameplay
	// so we set momentarelly all gamepad reports to 0.
	// If this disconnection happens during gameplay, the gamepad would be stuck in the last state.
	for (int i = 0; i < CONFIG_BLUEPAD32_MAX_DEVICES; i++) {
		empty_gamepad_report(&report[i]);
		idx_r.idx = i;
		idx_r.report = report[i];
		set_global_gamepad_report(&idx_r);
	}
	connected_controllers--;
	set_led_status();
}

static uni_error_t pico_switch_platform_on_device_ready(uni_hid_device_t* d) {
    logi("my_platform: device ready: %p\n", d);

	connected_controllers++;
	set_led_status();
    return UNI_ERROR_SUCCESS;
}

static void pico_switch_platform_on_controller_data(uni_hid_device_t* d, uni_controller_t* ctl)
{
	uint8_t idx = 0;
    CombinedControllerState* state = &combined_states[idx];

    // Update input state
    if (ctl->klass == UNI_CONTROLLER_CLASS_KEYBOARD) 
	{
        state->has_keyboard = true;
        state->keyboard = ctl->keyboard;
    } 
	else if (ctl->klass == UNI_CONTROLLER_CLASS_MOUSE) 
	{
        state->has_mouse = true;
        state->mouse = ctl->mouse;
    }

	//empty report
	empty_gamepad_report(&report[idx]);

	//fill report with new mouse and keyboard data
    if (state->has_keyboard)
	{
		fill_gamepad_report_from_keyboard(idx, &state->keyboard);
	}
        
    if (state->has_mouse)
	{
        fill_gamepad_report_from_mouse(idx, &state->mouse);
	}

    idx_r.idx = idx;
    idx_r.report = report[idx];
    set_global_gamepad_report(&idx_r);

}

static const uni_property_t* pico_switch_platform_get_property(uni_property_idx_t idx) {
    // Deprecated
    ARG_UNUSED(idx);
    return NULL;
}

static void pico_switch_platform_on_oob_event(uni_platform_oob_event_t event, void* data) {
	ARG_UNUSED(event);
	ARG_UNUSED(data);
	return;
}

//
// Helpers - UNUSED
//
static void trigger_event_on_gamepad(uni_hid_device_t* d) {
    if (d->report_parser.set_player_leds != NULL) {
        static uint8_t led = 0;
        led += 1;
        led &= 0xf;
        d->report_parser.set_player_leds(d, led);
    }

    if (d->report_parser.set_lightbar_color != NULL) {
        static uint8_t red = 0x10;
        static uint8_t green = 0x20;
        static uint8_t blue = 0x40;

        red += 0x10;
        green -= 0x20;
        blue += 0x40;
        d->report_parser.set_lightbar_color(d, red, green, blue);
    }
}

//
// Entry Point
//
struct uni_platform* get_my_platform(void) {
    static struct uni_platform plat = {
        .name = "My Platform",
        .init = pico_switch_platform_init,
        .on_init_complete = pico_switch_platform_on_init_complete,
        .on_device_connected = pico_switch_platform_on_device_connected,
        .on_device_disconnected = pico_switch_platform_on_device_disconnected,
        .on_device_ready = pico_switch_platform_on_device_ready,
        .on_oob_event = pico_switch_platform_on_oob_event,
        .on_controller_data = pico_switch_platform_on_controller_data,
        .get_property = pico_switch_platform_get_property,
    };
    return &plat;
}
