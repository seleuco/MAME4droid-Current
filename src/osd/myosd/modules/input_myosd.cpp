// license:BSD-3-Clause
//============================================================
//
//  input_myosd.cpp - myosd input modules: register the
//  keyboard/joystick/mouse/lightgun devices backed by g_input
//
//  MAME4DROID by David Valdeita (Seleuco)
//
//============================================================

// MAME headers
#include "emu.h"
#include "input.h"
#include "inputdev.h"

#include "modules/input/input_module.h"
#include "modules/osdmodule.h"

// MAMEOS headers
#include "myosd.h"
#include "myosd_platform.h"

static input_item_id key_map(myosd_keycode key);

//============================================================
//  get_xxx
//============================================================

static int get_key(void *device, void *item)
{
    return *(uint8_t*)item != 0;
}

static int get_axis(void *device, void *item)
{
    float value = *(float*)item;
    return (int)(value * osd::input_device::ABSOLUTE_MAX);
}

static int get_axis_neg(void *device, void *item)
{
    float value = *(float*)item;
    return (int)(value * osd::input_device::ABSOLUTE_MIN);
}

static int get_mouse(void *device, void *item)
{
    float value = *(float*)item;
    return (int)round(value);
}

#define BTN(off,mask) (void*)(intptr_t)(((intptr_t)mask<<32) | offsetof(myosd_input_state,off))
static int get_button(void *device, void *item)
{
    _Static_assert(sizeof(intptr_t) == 8, "");
    intptr_t off  = (intptr_t)item & 0xFFFFFFFF;
    intptr_t mask = (intptr_t)item>>32;
    unsigned long status = *(unsigned long *)((uint8_t*)&g_input + off);
    return (status & mask) != 0;
}

static constexpr input_code make_code(
        input_item_class itemclass,
input_item_modifier modifier,
        input_item_id item)
{
return input_code(DEVICE_CLASS_JOYSTICK, 0, itemclass, modifier, item);
}

//============================================================
//  input modules (platform-named providers) - the state lives in
//  g_input and is filled by the single host poll in
//  my_osd_interface::input_update, so poll_if_necessary is a no-op
//============================================================

namespace {

class keyboard_input_myosd : public osd_module, public input_module
{
public:
    keyboard_input_myosd() : osd_module(OSD_KEYBOARDINPUT_PROVIDER, MYOSD_PROVIDER_NAME) { }
    int init(osd_interface &osd, const osd_options &options) override { return 0; }
    void poll_if_necessary(bool relative_reset) override { }
    void input_init(running_machine &machine) override;
};

class joystick_input_myosd : public osd_module, public input_module
{
public:
    joystick_input_myosd() : osd_module(OSD_JOYSTICKINPUT_PROVIDER, MYOSD_PROVIDER_NAME) { }
    int init(osd_interface &osd, const osd_options &options) override { return 0; }
    void poll_if_necessary(bool relative_reset) override { }
    void input_init(running_machine &machine) override;
};

class mouse_input_myosd : public osd_module, public input_module
{
public:
    mouse_input_myosd() : osd_module(OSD_MOUSEINPUT_PROVIDER, MYOSD_PROVIDER_NAME) { }
    int init(osd_interface &osd, const osd_options &options) override { return 0; }
    void poll_if_necessary(bool relative_reset) override { }
    void input_init(running_machine &machine) override;
};

class lightgun_input_myosd : public osd_module, public input_module
{
public:
    lightgun_input_myosd() : osd_module(OSD_LIGHTGUNINPUT_PROVIDER, MYOSD_PROVIDER_NAME) { }
    int init(osd_interface &osd, const osd_options &options) override { return 0; }
    void poll_if_necessary(bool relative_reset) override { }
    void input_init(running_machine &machine) override;
};

//============================================================
//  keyboard
//============================================================

void keyboard_input_myosd::input_init(running_machine &machine)
{
    osd_printf_verbose("keyboard_input_myosd::input_init\n");

    char name[32];

    snprintf(name, sizeof(name)-1, "Keyboard");
    input_device* keyboard = &machine.input().device_class(DEVICE_CLASS_KEYBOARD).add_device(name, name);

    // all keys
    for (int key = MYOSD_KEY_FIRST; key <= MYOSD_KEY_LAST; key++)
    {
        input_item_id itemid = key_map((myosd_keycode)key);

        if (itemid == ITEM_ID_INVALID)
            continue;

        const char* name = machine.input().standard_token(itemid);
        keyboard->add_item(name, "", itemid, get_key, &g_input.keyboard[key]);
    }
}

//============================================================
//  joysticks
//============================================================

void joystick_input_myosd::input_init(running_machine &machine)
{
    osd_printf_verbose("joystick_input_myosd::input_init\n");

    machine.input().device_class(DEVICE_CLASS_JOYSTICK).enable(true);

    char name[32];

    for (int i=0; i<MYOSD_NUM_JOY; i++)
    {
        input_device::assignment_vector assignments;
        snprintf(name, sizeof(name)-1, "Joy %d", i+1);
        input_device* joystick = &machine.input().device_class(DEVICE_CLASS_JOYSTICK).add_device(name, name);

        joystick->add_item("LX Axis", "", ITEM_ID_XAXIS,  get_axis,     &g_input.joy_analog[i][MYOSD_AXIS_LX]);
        joystick->add_item("LY Axis", "", ITEM_ID_YAXIS,  get_axis_neg, &g_input.joy_analog[i][MYOSD_AXIS_LY]);
        joystick->add_item("LZ Axis", "", ITEM_ID_ZAXIS,  get_axis,     &g_input.joy_analog[i][MYOSD_AXIS_LZ]);
        joystick->add_item("RX Axis", "", ITEM_ID_RXAXIS, get_axis,     &g_input.joy_analog[i][MYOSD_AXIS_RX]);
        joystick->add_item("RY Axis", "", ITEM_ID_RYAXIS, get_axis_neg, &g_input.joy_analog[i][MYOSD_AXIS_RY]);
        joystick->add_item("RZ Axis", "", ITEM_ID_RZAXIS, get_axis,     &g_input.joy_analog[i][MYOSD_AXIS_RZ]);


		//left (ppal)
		assignments.emplace_back(IPT_JOYSTICKLEFT_LEFT,SEQ_TYPE_STANDARD,
                                 input_seq(make_code(ITEM_CLASS_SWITCH, ITEM_MODIFIER_LEFT, ITEM_ID_XAXIS)));
        assignments.emplace_back(IPT_JOYSTICKLEFT_RIGHT,SEQ_TYPE_STANDARD,
                                 input_seq(make_code(ITEM_CLASS_SWITCH, ITEM_MODIFIER_RIGHT, ITEM_ID_XAXIS)));
        assignments.emplace_back(IPT_JOYSTICKLEFT_UP,SEQ_TYPE_STANDARD,
                                 input_seq(make_code(ITEM_CLASS_SWITCH, ITEM_MODIFIER_UP, ITEM_ID_YAXIS)));
        assignments.emplace_back(IPT_JOYSTICKLEFT_DOWN,SEQ_TYPE_STANDARD,
                                 input_seq(make_code(ITEM_CLASS_SWITCH, ITEM_MODIFIER_DOWN, ITEM_ID_YAXIS)));
								 							 
		//right
        assignments.emplace_back(IPT_JOYSTICKRIGHT_LEFT,SEQ_TYPE_STANDARD,
                                 input_seq(make_code(ITEM_CLASS_SWITCH, ITEM_MODIFIER_NEG, ITEM_ID_RXAXIS)));
        assignments.emplace_back(IPT_JOYSTICKRIGHT_RIGHT,SEQ_TYPE_STANDARD,
                                 input_seq(make_code(ITEM_CLASS_SWITCH, ITEM_MODIFIER_POS, ITEM_ID_RXAXIS)));
        assignments.emplace_back(IPT_JOYSTICKRIGHT_UP,SEQ_TYPE_STANDARD,
                                 input_seq(make_code(ITEM_CLASS_SWITCH, ITEM_MODIFIER_NEG, ITEM_ID_RYAXIS)));
        assignments.emplace_back(IPT_JOYSTICKRIGHT_DOWN,SEQ_TYPE_STANDARD,
                                 input_seq(make_code(ITEM_CLASS_SWITCH, ITEM_MODIFIER_POS, ITEM_ID_RYAXIS)));								 
						 

        assignments.emplace_back(IPT_POSITIONAL,SEQ_TYPE_INCREMENT,
                                 input_seq(make_code(ITEM_CLASS_SWITCH, ITEM_MODIFIER_NONE, ITEM_ID_BUTTON5)));
        assignments.emplace_back(IPT_POSITIONAL,SEQ_TYPE_DECREMENT,
                                 input_seq(make_code(ITEM_CLASS_SWITCH, ITEM_MODIFIER_NONE, ITEM_ID_BUTTON6)));

        //assignments.emplace_back(IPT_SELECT,SEQ_TYPE_STANDARD,
         //                        input_seq(make_code(ITEM_CLASS_SWITCH, ITEM_MODIFIER_NONE, ITEM_ID_BUTTON2)));

        //joystick->add_item("LX Axis ABS1", "", ITEM_ID_ADD_ABSOLUTE1,  get_axis,     &g_input.joy_analog[i][MYOSD_AXIS_LX]);
        //joystick->add_item("LY Axis ABS2", "", ITEM_ID_ADD_ABSOLUTE2,  get_axis_neg, &g_input.joy_analog[i][MYOSD_AXIS_LY]);

        joystick->add_item("A", "", ITEM_ID_BUTTON1, get_button, BTN(joy_status[i], MYOSD_A));
        joystick->add_item("B", "", ITEM_ID_BUTTON2, get_button, BTN(joy_status[i], MYOSD_B));

        //joystick->add_item("A SW1", "", ITEM_ID_ADD_SWITCH1, get_button, BTN(joy_status[i], MYOSD_A));
        //joystick->add_item("B SW2", "", ITEM_ID_ADD_SWITCH2, get_button, BTN(joy_status[i], MYOSD_B));

        joystick->add_item("C", "", ITEM_ID_BUTTON3, get_button, BTN(joy_status[i], MYOSD_C));
        joystick->add_item("D", "", ITEM_ID_BUTTON4, get_button, BTN(joy_status[i], MYOSD_D));
        joystick->add_item("E", "", ITEM_ID_BUTTON5, get_button, BTN(joy_status[i], MYOSD_L1));
        joystick->add_item("F", "", ITEM_ID_BUTTON6, get_button, BTN(joy_status[i], MYOSD_R1));

        joystick->add_item("G", "", ITEM_ID_BUTTON7, get_button, BTN(joy_status[i], MYOSD_L2));
        joystick->add_item("H", "", ITEM_ID_BUTTON8, get_button, BTN(joy_status[i], MYOSD_R2));
        //joystick->add_item("L3", "", ITEM_ID_BUTTON9, get_button, BTN(joy_status[i], MYOSD_L3));
        //joystick->add_item("R3", "", ITEM_ID_BUTTON10,get_button, BTN(joy_status[i], MYOSD_R3));

        joystick->add_item("Select", "", ITEM_ID_SELECT, get_button, BTN(joy_status[i], MYOSD_SELECT));
        joystick->add_item("Start",  "", ITEM_ID_START,  get_button, BTN(joy_status[i], MYOSD_START));

        joystick->add_item("D-Pad Up",    "", ITEM_ID_HAT1UP,    get_button, BTN(joy_status[i], MYOSD_UP));
        joystick->add_item("D-Pad Down",  "", ITEM_ID_HAT1DOWN,  get_button, BTN(joy_status[i], MYOSD_DOWN));
        joystick->add_item("D-Pad Left",  "", ITEM_ID_HAT1LEFT,  get_button, BTN(joy_status[i], MYOSD_LEFT));
        joystick->add_item("D-Pad Right", "", ITEM_ID_HAT1RIGHT, get_button, BTN(joy_status[i], MYOSD_RIGHT));

        joystick->set_default_assignments(std::move(assignments));
    }
}

//============================================================
//  mice
//============================================================

void mouse_input_myosd::input_init(running_machine &machine)
{
    osd_printf_verbose("mouse_input_myosd::input_init\n");

    machine.input().device_class(DEVICE_CLASS_MOUSE).enable(true);

    char name[32];

    for (int i=0; i<MYOSD_NUM_MICE; i++)
    {
        snprintf(name, sizeof(name)-1, "Mouse %d", i+1);
        input_device* mouse = &machine.input().device_class(DEVICE_CLASS_MOUSE).add_device(name, name);

        mouse->add_item("X Axis", "", ITEM_ID_XAXIS, get_mouse, &g_input.mouse_x[i]);
        mouse->add_item("Y Axis", "", ITEM_ID_YAXIS, get_mouse, &g_input.mouse_y[i]);
        mouse->add_item("Z Axis", "", ITEM_ID_ZAXIS, get_mouse, &g_input.mouse_z[i]);
        mouse->add_item("Left",   "", ITEM_ID_BUTTON1, get_button, BTN(mouse_status[i], MYOSD_A));
        mouse->add_item("Middle", "", ITEM_ID_BUTTON2, get_button, BTN(mouse_status[i], MYOSD_D));
        mouse->add_item("Right",  "", ITEM_ID_BUTTON3, get_button, BTN(mouse_status[i], MYOSD_B));
    }
}

//============================================================
//  lightguns
//============================================================

void lightgun_input_myosd::input_init(running_machine &machine)
{
    osd_printf_verbose("lightgun_input_myosd::input_init\n");

    machine.input().device_class(DEVICE_CLASS_LIGHTGUN).enable(true);

    char name[32];

    for (int i=0; i<MYOSD_NUM_GUN; i++)
    {
        snprintf(name, sizeof(name)-1, "Lightgun %d", i+1);
        input_device* lightgun = &machine.input().device_class(DEVICE_CLASS_LIGHTGUN).add_device(name, name);

        lightgun->add_item("X Axis", "", ITEM_ID_XAXIS, get_axis, &g_input.lightgun_x[i]);
        lightgun->add_item("Y Axis", "", ITEM_ID_YAXIS, get_axis_neg, &g_input.lightgun_y[i]);
        lightgun->add_item("A", "", ITEM_ID_BUTTON1, get_button, BTN(lightgun_status[i], MYOSD_A));
        lightgun->add_item("B", "", ITEM_ID_BUTTON2, get_button, BTN(lightgun_status[i], MYOSD_B));
    }
}

} // anonymous namespace


MODULE_DEFINITION(KEYBOARDINPUT_MYOSD, keyboard_input_myosd)
MODULE_DEFINITION(JOYSTICKINPUT_MYOSD, joystick_input_myosd)
MODULE_DEFINITION(MOUSEINPUT_MYOSD, mouse_input_myosd)
MODULE_DEFINITION(LIGHTGUNINPUT_MYOSD, lightgun_input_myosd)

//============================================================
//  convert myosd_keycode -> input_item_id
//============================================================

#define LERPKEY(key, key0, key1, item0, item1) \
    _Static_assert((key1 - key0) == ((int)item1 - (int)item0), ""); \
    if (key >= key0 && key <= key1) \
        return (input_item_id)((int)item0 + (key - key0));

#define MAPKEY(key, A, B) \
    LERPKEY(key, MYOSD_KEY_ ## A, MYOSD_KEY_ ## B, ITEM_ID_ ## A, ITEM_ID_ ## B)

static input_item_id key_map(myosd_keycode key)
{
    _Static_assert(MYOSD_KEY_FIRST == MYOSD_KEY_A, "");
    _Static_assert(MYOSD_KEY_LAST  == MYOSD_KEY_CANCEL, "");

    MAPKEY(key, A, F15)         // missing F16-F20
    MAPKEY(key, ESC, ENTER_PAD) // missing BS_PAD, TAB_PAD, 00_PAD, 000_PAD, COMMA_PAD, EQUALS_PAD
    MAPKEY(key, PRTSCR, CANCEL)

    return ITEM_ID_INVALID;
}
