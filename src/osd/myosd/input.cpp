// license:BSD-3-Clause
//============================================================
//
//  myosd-droid.cpp - osd input handling
//
//  MAME4DROID  by David Valdeita (Seleuco)
//
//============================================================

// MAME headers
#include "emu.h"
#include "inputdev.h"
#include "ui/uimain.h"
//#include "emuopts.h"
//#include "uiinput.h"

#include "../modules/input/assignmenthelper.h"

// include this to get mame_ui_manager, sigh
//#include "../frontend/mame/ui/ui.h"


// MAMEOS headers
#include "myosd.h"

static input_item_id key_map(myosd_keycode key);

//============================================================
// global input state
//============================================================
static myosd_input_state g_input;

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
//  init
//============================================================

void my_osd_interface::input_init()
{
    osd_printf_verbose("my_osd_interface::input_init\n");

    machine().input().device_class(DEVICE_CLASS_JOYSTICK).enable(true);
    machine().input().device_class(DEVICE_CLASS_MOUSE).enable(true);
    machine().input().device_class(DEVICE_CLASS_LIGHTGUN).enable(true);

    // clear input state, this will cause input_update_init to be called later
    memset(&g_input, 0, sizeof(g_input));

    char name[32];

    // keyboard
    snprintf(name, sizeof(name)-1, "Keyboard");
    input_device* keyboard = &machine().input().device_class(DEVICE_CLASS_KEYBOARD).add_device(name, name);

    // all keys
    for (int key = MYOSD_KEY_FIRST; key <= MYOSD_KEY_LAST; key++)
    {
        input_item_id itemid = key_map((myosd_keycode)key);

        if (itemid == ITEM_ID_INVALID)
            continue;

        const char* name = machine().input().standard_token(itemid);
        keyboard->add_item(name, "", itemid, get_key, &g_input.keyboard[key]);
    }

    // joystick
    for (int i=0; i<MYOSD_NUM_JOY; i++)
    {
        input_device::assignment_vector assignments;
        snprintf(name, sizeof(name)-1, "Joy %d", i+1);
        input_device* joystick = &machine().input().device_class(DEVICE_CLASS_JOYSTICK).add_device(name, name);

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

    // mice
    for (int i=0; i<MYOSD_NUM_MICE; i++)
    {
        snprintf(name, sizeof(name)-1, "Mouse %d", i+1);
        input_device* mouse = &machine().input().device_class(DEVICE_CLASS_MOUSE).add_device(name, name);

        mouse->add_item("X Axis", "", ITEM_ID_XAXIS, get_mouse, &g_input.mouse_x[i]);
        mouse->add_item("Y Axis", "", ITEM_ID_YAXIS, get_mouse, &g_input.mouse_y[i]);
        mouse->add_item("Z Axis", "", ITEM_ID_ZAXIS, get_mouse, &g_input.mouse_z[i]);
        mouse->add_item("Left",   "", ITEM_ID_BUTTON1, get_button, BTN(mouse_status[i], MYOSD_A));
        mouse->add_item("Middle", "", ITEM_ID_BUTTON2, get_button, BTN(mouse_status[i], MYOSD_D));
        mouse->add_item("Right",  "", ITEM_ID_BUTTON3, get_button, BTN(mouse_status[i], MYOSD_B));
    }

    // lightgun
    for (int i=0; i<MYOSD_NUM_GUN; i++)
    {
        snprintf(name, sizeof(name)-1, "Lightgun %d", i+1);
        input_device* lightgun = &machine().input().device_class(DEVICE_CLASS_LIGHTGUN).add_device(name, name);

        lightgun->add_item("X Axis", "", ITEM_ID_XAXIS, get_axis, &g_input.lightgun_x[i]);
        lightgun->add_item("Y Axis", "", ITEM_ID_YAXIS, get_axis_neg, &g_input.lightgun_y[i]);
        lightgun->add_item("A", "", ITEM_ID_BUTTON1, get_button, BTN(lightgun_status[i], MYOSD_A));
        lightgun->add_item("B", "", ITEM_ID_BUTTON2, get_button, BTN(lightgun_status[i], MYOSD_B));
    }


}

//============================================================
//  exit
//============================================================

void my_osd_interface::input_exit()
{
    osd_printf_verbose("my_osd_interface::input_exit\n");

    if (m_callbacks.input_exit != nullptr)
        m_callbacks.input_exit();
}

//============================================================
//  input init
//============================================================
void input_profile_init(running_machine &machine)
{
    osd_printf_verbose("input_profile_init: %d players=%d safe=%d\n", machine.ioport().ports().size(), machine.ioport().count_players(), machine.ioport().safe_to_read());

    if (machine.ioport().ports().size() == 0)
    {
        // default empty machine
        g_input.num_players = 0;
        g_input.num_coins = 0;
        g_input.num_inputs = 1;
        g_input.num_ways = 4;
        g_input.num_buttons = 0;
        g_input.num_mouse = 0;
        g_input.num_lightgun = 0;
        g_input.num_keyboard = 0;
    }
    else
    {
        // assume an 8way by default, unless we find a 4way
        g_input.num_ways = 8;
        for (auto &port : machine.ioport().ports())
        {
            for (ioport_field &field : port.second->fields())
            {
                if (field.way() == 4)
                    g_input.num_ways = 4;
            }
        }

        int way8 = 0;
        g_input.num_buttons = 0;
        g_input.num_lightgun = 0;
        g_input.num_mouse = 0;
        g_input.num_keyboard = 0;
        g_input.num_players = 0;
        g_input.num_coins = 0;
        g_input.num_inputs = 0;

        for (auto &port : machine.ioport().ports())
        {
            osd_printf_verbose("    PORT:%s\n", port.first);

            for (ioport_field &field : port.second->fields())
            {
                osd_printf_verbose("        FIELD:%s player=%d type=%d way=%d\n", field.name(), field.player(), field.type(), field.way());

                // walk the input seq and look for highest device/joystick
                if ((field.type() >= IPT_DIGITAL_JOYSTICK_FIRST && field.type() <= IPT_DIGITAL_JOYSTICK_LAST) ||
                    (field.type() >= IPT_BUTTON1 && field.type() <= IPT_BUTTON12))
                {
                    input_seq const seq = field.seq(SEQ_TYPE_STANDARD);
                    for (int i=0; seq[i] != input_seq::end_code; i++)
                    {
                        input_code code = seq[i];
                        if (code.device_index() >= g_input.num_inputs)
                            g_input.num_inputs = code.device_index()+1;
                    }
                }

                // walk the input seq and look for highest analog device/joystick
                if (field.type() >= IPT_ANALOG_FIRST && field.type() <= IPT_ANALOG_LAST)
                {
                    input_seq const seq = field.seq(SEQ_TYPE_STANDARD);
                    for (int i=0; seq[i] != input_seq::end_code; i++)
                    {
                        input_code code = seq[i];
                        if (code.device_class() == DEVICE_CLASS_JOYSTICK && code.device_index() >= g_input.num_inputs)
                            g_input.num_inputs = code.device_index()+1;
                        if (code.device_class() == DEVICE_CLASS_MOUSE && code.device_index() >= g_input.num_mouse)
                            g_input.num_mouse = code.device_index()+1;
                        if (code.device_class() == DEVICE_CLASS_LIGHTGUN && code.device_index() >= g_input.num_lightgun)
                            g_input.num_lightgun = code.device_index()+1;
                    }
                }

                // count the number of COIN buttons.
                if (field.type() == IPT_COIN1 && g_input.num_coins < 1)
                    g_input.num_coins = 1;
                if (field.type() == IPT_COIN2 && g_input.num_coins < 2)
                    g_input.num_coins = 2;
                if (field.type() == IPT_COIN3 && g_input.num_coins < 3)
                    g_input.num_coins = 3;
                if (field.type() == IPT_COIN4 && g_input.num_coins < 4)
                    g_input.num_coins = 4;
                if (field.type() == IPT_SELECT && g_input.num_coins < field.player()+1)
                    g_input.num_coins = field.player()+1;

                // count the number of players, by looking at the number of START buttons.
                if (field.type() == IPT_START1 && g_input.num_players < 1)
                    g_input.num_players = 1;
                if (field.type() == IPT_START2 && g_input.num_players < 2)
                    g_input.num_players = 2;
                if (field.type() == IPT_START3 && g_input.num_players < 3)
                    g_input.num_players = 3;
                if (field.type() == IPT_START4 && g_input.num_players < 4)
                    g_input.num_players = 4;
                if (field.type() == IPT_START && g_input.num_players < field.player()+1)
                    g_input.num_players = field.player()+1;

                if (field.player() != 0)
                    continue;   // only count ways and buttons for Player 1

                // count the number of buttons
                if(field.type() == IPT_BUTTON1)
                    if(g_input.num_buttons<1)g_input.num_buttons=1;
                if(field.type() == IPT_BUTTON2)
                    if(g_input.num_buttons<2)g_input.num_buttons=2;
                if(field.type() == IPT_BUTTON3)
                    if(g_input.num_buttons<3)g_input.num_buttons=3;
                if(field.type() == IPT_BUTTON4)
                    if(g_input.num_buttons<4)g_input.num_buttons=4;
                if(field.type() == IPT_BUTTON5)
                    if(g_input.num_buttons<5)g_input.num_buttons=5;
                if(field.type() == IPT_BUTTON6)
                    if(g_input.num_buttons<6)g_input.num_buttons=6;
                if(field.type() == IPT_JOYSTICKRIGHT_UP)//dual stick is mapped as buttons
                    if(g_input.num_buttons<4)g_input.num_buttons=4;
                if(field.type() == IPT_POSITIONAL)//positional is mapped with two last buttons
                    if(g_input.num_buttons<6)g_input.num_buttons=6;

                // count the number of ways (joystick directions)
                if(field.type() == IPT_JOYSTICK_UP || field.type() == IPT_JOYSTICK_DOWN || field.type() == IPT_JOYSTICKLEFT_UP || field.type() == IPT_JOYSTICKLEFT_DOWN)
                    way8=1;
                if(field.type() == IPT_AD_STICK_X || field.type() == IPT_LIGHTGUN_X || field.type() == IPT_MOUSE_X ||
                   field.type() == IPT_TRACKBALL_X || field.type() == IPT_PEDAL)
                    way8=1;

                // detect if mouse or lightgun input is wanted.
                if (g_input.num_mouse == 0 && (
                    field.type() == IPT_DIAL   || field.type() == IPT_PADDLE   /*|| field.type() == IPT_POSITIONAL*/   || field.type() == IPT_TRACKBALL_X || field.type() == IPT_MOUSE_X ||
                    field.type() == IPT_DIAL_V || field.type() == IPT_PADDLE_V /*|| field.type() == IPT_POSITIONAL_V*/ || field.type() == IPT_TRACKBALL_Y || field.type() == IPT_MOUSE_Y ))
                    g_input.num_mouse = 1;
                if(g_input.num_lightgun == 0 && field.type() == IPT_LIGHTGUN_X)
                    g_input.num_lightgun = 1;
                if(field.type() == IPT_KEYBOARD)
                    g_input.num_keyboard = 1;
            }
        }

        if(g_input.num_keyboard)
            g_input.num_mouse = 0;//no queremos raton touch en micro

        if(std::string(machine.system().type.source()).find("taito_f3") != std::string::npos && std::string(machine.system().name).find("arkretrn") == std::string::npos)
            g_input.num_mouse = 0;//ban taito has dial why??

        // 8 if analog or lightgun or up or down
        if (g_input.num_ways != 4) {
            if (way8)
                g_input.num_ways = 8;
            else
                g_input.num_ways = 2;
        }
    }

    osd_printf_debug("Num Buttons %d\n",g_input.num_buttons);
    osd_printf_debug("Num WAYS %d\n",g_input.num_ways);
    osd_printf_debug("Num PLAYERS %d\n",g_input.num_players);
    osd_printf_debug("Num COINS %d\n",g_input.num_coins);
    osd_printf_debug("Num INPUTS %d\n",g_input.num_inputs);
    osd_printf_debug("Num MOUSE %d\n",g_input.num_mouse);
    osd_printf_debug("Num GUN %d\n",g_input.num_lightgun);
    osd_printf_debug("Num KEYBOARD %d\n",g_input.num_keyboard);
}

//============================================================
//  update
//============================================================
void my_osd_interface::input_update(bool relative_reset)
{
    osd_printf_verbose("my_osd_interface::input_update\n");

    // fill in the input profile the first time
    if (g_input.num_ways == 0) {

        if (!machine().ioport().safe_to_read())
            return;

        input_profile_init(machine());

        if (m_callbacks.input_init != nullptr)
            m_callbacks.input_init(&g_input, sizeof(g_input));
    }

    bool in_menu = machine().phase() == machine_phase::RUNNING && machine().ui().is_menu_active();

    // determine if we should disable the rest of the UI
    bool has_keyboard = g_input.num_keyboard != 0; // machine_info().has_keyboard();

    //mame_ui_manager *ui = dynamic_cast<mame_ui_manager *>(&machine.ui());
    //bool ui_disabled = (has_keyboard && !ui->ui_active());
    bool ui_disabled = (has_keyboard && false);

    // set the current input mode
    g_input.input_mode = in_menu ? MYOSD_INPUT_MODE_MENU : (ui_disabled ? MYOSD_INPUT_MODE_KEYBOARD : MYOSD_INPUT_MODE_NORMAL);

    // and call host
    if (m_callbacks.input_poll != nullptr)
        m_callbacks.input_poll(relative_reset,&g_input, sizeof(g_input));
	
    // see if host requested an exit or reset
    if (g_input.keyboard[MYOSD_KEY_EXIT] != 0 && !machine().exit_pending())
        machine().schedule_exit();

    if (g_input.keyboard[MYOSD_KEY_RESET] != 0 && !machine().hard_reset_pending())
        machine().schedule_hard_reset();
}

//============================================================
//  check_osd_inputs
//============================================================

void my_osd_interface::check_osd_inputs()
{
}

//============================================================
//  customize_input_type_list
//============================================================


void my_osd_interface::customize_input_type_list(std::vector<input_type_entry> &typelist)
{
    // This function is called on startup, before reading the
    // configuration from disk. Scan the list, and change the
    // default control mappings you want. It is quite possible
    // you won't need to change a thing.

    //osd_printf_debug("INPUT TYPE LIST\n");

    // loop over the defaults
    for (input_type_entry &entry : typelist) {

        //osd_printf_debug("  %s TYPE:%d PLAYER:%d\n", entry.name() ?: "", entry.type(), entry.player());

        switch (entry.type())
        {

            case IPT_UI_SELECT://DAV
                //entry.defseq(SEQ_TYPE_STANDARD) |= JOYCODE_BUTTON1;
                entry.defseq(SEQ_TYPE_STANDARD) |= JOYCODE_BUTTON2;//ANDROID default
                break;
            case IPT_UI_BACK://DAV
                //entry.defseq(SEQ_TYPE_STANDARD) |= JOYCODE_BUTTON2;
                entry.defseq(SEQ_TYPE_STANDARD) |= JOYCODE_BUTTON1;//ANDROID default
                break;
            case IPT_UI_MENU://DAV
                //entry.defseq(SEQ_TYPE_STANDARD).set(KEYCODE_TAB, input_seq::or_code, JOYCODE_START , JOYCODE_SELECT);
                entry.defseq(SEQ_TYPE_STANDARD) |= (JOYCODE_START);
                entry.defseq(SEQ_TYPE_STANDARD) += (JOYCODE_SELECT);

                break;
            case IPT_UI_FOCUS_NEXT://DAV
                //entry.defseq(SEQ_TYPE_STANDARD).set(JOYCODE_START , JOYCODE_SELECT);
                //entry.defseq(SEQ_TYPE_STANDARD).set(JOYCODE_BUTTON3);
                entry.defseq(SEQ_TYPE_STANDARD).set(JOYCODE_SELECT);
                break;

            case IPT_UI_FOCUS_PREV://DAV
                //entry.defseq(SEQ_TYPE_STANDARD).set(JOYCODE_START , JOYCODE_SELECT);
                //entry.defseq(SEQ_TYPE_STANDARD).set(JOYCODE_BUTTON4);
                entry.defseq(SEQ_TYPE_STANDARD).set(JOYCODE_START);
                break;

            case IPT_BUTTON1:
                entry.defseq(SEQ_TYPE_STANDARD)  |= JOYCODE_BUTTON1_INDEXED(entry.player());
                break;
            case IPT_BUTTON2:
                entry.defseq(SEQ_TYPE_STANDARD) |= JOYCODE_BUTTON2_INDEXED(entry.player());
                break;
            case IPT_BUTTON3:
                entry.defseq(SEQ_TYPE_STANDARD) |= JOYCODE_BUTTON3_INDEXED(entry.player());
                entry.defseq(SEQ_TYPE_STANDARD) |= (MOUSECODE_BUTTON3);//prueba outrun
                break;
            case IPT_BUTTON4:
                entry.defseq(SEQ_TYPE_STANDARD) |= JOYCODE_BUTTON4_INDEXED(entry.player());
                break;
            case IPT_BUTTON5:
                entry.defseq(SEQ_TYPE_STANDARD) |= JOYCODE_BUTTON5_INDEXED(entry.player());
                break;
            case IPT_BUTTON6:
                entry.defseq(SEQ_TYPE_STANDARD) |= JOYCODE_BUTTON6_INDEXED(entry.player());
                break;
            case IPT_BUTTON7:
                entry.defseq(SEQ_TYPE_STANDARD) |= JOYCODE_BUTTON7_INDEXED(entry.player());
                break;
            case IPT_BUTTON8:
                entry.defseq(SEQ_TYPE_STANDARD) |= JOYCODE_BUTTON8_INDEXED(entry.player());
                break;

            case IPT_UI_FAST_FORWARD: //fast forward es start+a
                entry.defseq(SEQ_TYPE_STANDARD) |= (JOYCODE_START);
                entry.defseq(SEQ_TYPE_STANDARD) += (JOYCODE_BUTTON1);
                break;
            case IPT_UI_TOGGLE_UI: //UI lock es start+B
                entry.defseq(SEQ_TYPE_STANDARD) |= (JOYCODE_START);
                entry.defseq(SEQ_TYPE_STANDARD) += (JOYCODE_BUTTON2);
                break;
            case IPT_UI_CLEAR: //borrar añade start OK
                entry.defseq(SEQ_TYPE_STANDARD) |= (JOYCODE_START);
                entry.defseq(SEQ_TYPE_STANDARD) += (JOYCODE_BUTTON3);
                break;
            case IPT_UI_LOAD_STATE: //start + r1 load
                entry.defseq(SEQ_TYPE_STANDARD) |= (JOYCODE_START);
                entry.defseq(SEQ_TYPE_STANDARD) += (JOYCODE_BUTTON5);
                break;
            case IPT_UI_SAVE_STATE: //start + l1 save
                entry.defseq(SEQ_TYPE_STANDARD) |= (JOYCODE_START);
                entry.defseq(SEQ_TYPE_STANDARD) += (JOYCODE_BUTTON6);
                break;
            case IPT_UI_PAUSE: //no P as Pause
                //entry.defseq(SEQ_TYPE_STANDARD) = input_code();//no P default so systems natural keboards don't hung
                //entry.defseq(SEQ_TYPE_STANDARD) |= KEYCODE_PAUSE;

                break;
            case IPT_SERVICE: //START+A+B is service
                entry.defseq(SEQ_TYPE_STANDARD) |= (JOYCODE_START);
                entry.defseq(SEQ_TYPE_STANDARD) += (JOYCODE_BUTTON1);
                entry.defseq(SEQ_TYPE_STANDARD) += (JOYCODE_BUTTON2);
                break;
            case IPT_UI_SOFT_RESET: //SELECT+A+B is soft reset
                entry.defseq(SEQ_TYPE_STANDARD) |= (JOYCODE_SELECT);
                entry.defseq(SEQ_TYPE_STANDARD) += (JOYCODE_BUTTON1);
                entry.defseq(SEQ_TYPE_STANDARD) += (JOYCODE_BUTTON2);
                break;

            //SELECT+UP/RIGHT/DOWN insert 2P/3P/4P credit
            case IPT_COIN2:
                entry.defseq(SEQ_TYPE_STANDARD) |= (JOYCODE_SELECT);
                entry.defseq(SEQ_TYPE_STANDARD) += JOYCODE_HAT1UP_INDEXED(0);
                break;
            case IPT_COIN3:
                entry.defseq(SEQ_TYPE_STANDARD) |= (JOYCODE_SELECT);
                entry.defseq(SEQ_TYPE_STANDARD) += JOYCODE_HAT1RIGHT_INDEXED(0);
                break;
            case IPT_COIN4:
                entry.defseq(SEQ_TYPE_STANDARD) |= (JOYCODE_SELECT);
                entry.defseq(SEQ_TYPE_STANDARD) += JOYCODE_HAT1DOWN_INDEXED(0);
                break;

            //START+UP/RIGHT/DOWN starts 2P/3P/4P game
            case IPT_START2:
                entry.defseq(SEQ_TYPE_STANDARD) |= (JOYCODE_START);
                entry.defseq(SEQ_TYPE_STANDARD) += JOYCODE_HAT1UP_INDEXED(0);
                break;
            case IPT_START3:
                entry.defseq(SEQ_TYPE_STANDARD) |= (JOYCODE_START);
                entry.defseq(SEQ_TYPE_STANDARD) += JOYCODE_HAT1RIGHT_INDEXED(0);
                break;
            case IPT_START4:
                entry.defseq(SEQ_TYPE_STANDARD) |= (JOYCODE_START);
                entry.defseq(SEQ_TYPE_STANDARD) += JOYCODE_HAT1DOWN_INDEXED(0);
                break;

            case IPT_UI_PASTE:
                entry.defseq(SEQ_TYPE_STANDARD).set(KEYCODE_SCRLOCK, KEYCODE_LSHIFT);
                break;

            // allow the DPAD to move the UI
            case IPT_UI_UP:
                entry.defseq(SEQ_TYPE_STANDARD) |= JOYCODE_HAT1UP_INDEXED(0);
                entry.defseq(SEQ_TYPE_STANDARD) |= JOYCODE_Y_UP_SWITCH_INDEXED(0);
                break;
            case IPT_UI_DOWN:
                entry.defseq(SEQ_TYPE_STANDARD) |= JOYCODE_HAT1DOWN_INDEXED(0);
                entry.defseq(SEQ_TYPE_STANDARD) |= JOYCODE_Y_DOWN_SWITCH_INDEXED(0);
                break;
            case IPT_UI_LEFT:
                entry.defseq(SEQ_TYPE_STANDARD) |= JOYCODE_HAT1LEFT_INDEXED(0);
                entry.defseq(SEQ_TYPE_STANDARD) |= JOYCODE_X_LEFT_SWITCH_INDEXED(0);
                break;
            case IPT_UI_RIGHT:
                entry.defseq(SEQ_TYPE_STANDARD) |= JOYCODE_HAT1RIGHT_INDEXED(0);
                entry.defseq(SEQ_TYPE_STANDARD) |= JOYCODE_X_RIGHT_SWITCH_INDEXED(0);
                break;

            /* allow L1 and R1 to move pages in MAME UI */
            case IPT_UI_PAGE_UP:
                entry.defseq(SEQ_TYPE_STANDARD) |= JOYCODE_BUTTON5;

                entry.defseq(SEQ_TYPE_STANDARD) |= (JOYCODE_BUTTON1);
                entry.defseq(SEQ_TYPE_STANDARD) += JOYCODE_HAT1UP_INDEXED(0);

                entry.defseq(SEQ_TYPE_STANDARD) |= (JOYCODE_BUTTON1);
                entry.defseq(SEQ_TYPE_STANDARD) += JOYCODE_V_NEG_SWITCH_INDEXED(0);
                break;
            case IPT_UI_PAGE_DOWN:
                entry.defseq(SEQ_TYPE_STANDARD) |= JOYCODE_BUTTON6;

                entry.defseq(SEQ_TYPE_STANDARD) |= (JOYCODE_BUTTON1);
                entry.defseq(SEQ_TYPE_STANDARD) += JOYCODE_HAT1DOWN_INDEXED(0);

                entry.defseq(SEQ_TYPE_STANDARD) |= (JOYCODE_BUTTON1);
                entry.defseq(SEQ_TYPE_STANDARD) += JOYCODE_V_POS_SWITCH_INDEXED(0);
                break;

            /* LEFT Joystick these are mostly the same as MAME defaults, except we add dpad to them */
			case IPT_JOYSTICK_UP:
                entry.defseq(SEQ_TYPE_STANDARD) |= JOYCODE_HAT1UP_INDEXED(entry.player());
                entry.defseq(SEQ_TYPE_STANDARD) |= JOYCODE_Y_UP_SWITCH_INDEXED(entry.player());
                break;
            case IPT_JOYSTICK_DOWN:
                entry.defseq(SEQ_TYPE_STANDARD) |= JOYCODE_HAT1DOWN_INDEXED(entry.player());
                entry.defseq(SEQ_TYPE_STANDARD) |= JOYCODE_Y_DOWN_SWITCH_INDEXED(entry.player());
                break;
            case IPT_JOYSTICK_LEFT:
                entry.defseq(SEQ_TYPE_STANDARD) |= JOYCODE_HAT1LEFT_INDEXED(entry.player());
                entry.defseq(SEQ_TYPE_STANDARD) |= JOYCODE_X_LEFT_SWITCH_INDEXED(entry.player());
                break;
            case IPT_JOYSTICK_RIGHT:
                entry.defseq(SEQ_TYPE_STANDARD) |= JOYCODE_HAT1RIGHT_INDEXED(entry.player());
                entry.defseq(SEQ_TYPE_STANDARD) |= JOYCODE_X_RIGHT_SWITCH_INDEXED(entry.player());
                break;

            /* --- TWIN-STICK (Smash TV, Total Carnage, etc.) --- */
            case IPT_JOYSTICKLEFT_UP:
                entry.defseq(SEQ_TYPE_STANDARD) |= JOYCODE_HAT1UP_INDEXED(entry.player());
                break;
            case IPT_JOYSTICKLEFT_DOWN:
                entry.defseq(SEQ_TYPE_STANDARD) |= JOYCODE_HAT1DOWN_INDEXED(entry.player());
                break;
            case IPT_JOYSTICKLEFT_LEFT:
                entry.defseq(SEQ_TYPE_STANDARD) |= JOYCODE_HAT1LEFT_INDEXED(entry.player());
                break;
            case IPT_JOYSTICKLEFT_RIGHT:
                entry.defseq(SEQ_TYPE_STANDARD) |= JOYCODE_HAT1RIGHT_INDEXED(entry.player());
                break;
								
			//RIGHT Joystick 
			//also map right stick to buttons so dual games like robotron could be played? should be configurable or only triger by touch
			case IPT_JOYSTICKRIGHT_UP:
                entry.defseq(SEQ_TYPE_STANDARD) |= JOYCODE_BUTTON4_INDEXED(entry.player());
                //entry.defseq(SEQ_TYPE_STANDARD) |= input_code(DEVICE_CLASS_JOYSTICK, entry.player(), ITEM_CLASS_SWITCH, ITEM_MODIFIER_NEG, ITEM_ID_RYAXIS);
                break;
            case IPT_JOYSTICKRIGHT_DOWN:
                entry.defseq(SEQ_TYPE_STANDARD) |= JOYCODE_BUTTON2_INDEXED(entry.player());
                //entry.defseq(SEQ_TYPE_STANDARD) |= input_code(DEVICE_CLASS_JOYSTICK, entry.player(), ITEM_CLASS_SWITCH, ITEM_MODIFIER_POS, ITEM_ID_RYAXIS);
                break;
            case IPT_JOYSTICKRIGHT_LEFT:
                entry.defseq(SEQ_TYPE_STANDARD) |= JOYCODE_BUTTON3_INDEXED(entry.player());
                //entry.defseq(SEQ_TYPE_STANDARD) |= input_code(DEVICE_CLASS_JOYSTICK, entry.player(), ITEM_CLASS_SWITCH, ITEM_MODIFIER_NEG, ITEM_ID_RXAXIS);
                break;
            case IPT_JOYSTICKRIGHT_RIGHT:
                entry.defseq(SEQ_TYPE_STANDARD) |= JOYCODE_BUTTON1_INDEXED(entry.player());
                //entry.defseq(SEQ_TYPE_STANDARD) |= input_code(DEVICE_CLASS_JOYSTICK, entry.player(), ITEM_CLASS_SWITCH, ITEM_MODIFIER_POS, ITEM_ID_RXAXIS);
                break;

            //map adstick,dial, trackball
            case IPT_AD_STICK_Y:
            case IPT_TRACKBALL_Y:
            case IPT_LIGHTGUN_Y:
            case IPT_DIAL_V:
            case IPT_PADDLE_V:
            //case IPT_POSITIONAL_V:
                entry.defseq(SEQ_TYPE_STANDARD) |= JOYCODE_Y_INDEXED(entry.player());
                if(entry.type()!=IPT_LIGHTGUN_Y) {
                    entry.defseq(SEQ_TYPE_INCREMENT) |= JOYCODE_HAT1DOWN_INDEXED(entry.player());
                    entry.defseq(SEQ_TYPE_DECREMENT) |= JOYCODE_HAT1UP_INDEXED(entry.player());
                }
                break;
            case IPT_AD_STICK_X:
            case IPT_TRACKBALL_X:
            case IPT_LIGHTGUN_X:
            case IPT_DIAL:
            case IPT_PADDLE:
				entry.defseq(SEQ_TYPE_STANDARD) |= JOYCODE_X_INDEXED(entry.player());
                if(entry.type()!=IPT_LIGHTGUN_X) {
                    entry.defseq(SEQ_TYPE_INCREMENT) |= JOYCODE_HAT1RIGHT_INDEXED(entry.player());
                    entry.defseq(SEQ_TYPE_DECREMENT) |= JOYCODE_HAT1LEFT_INDEXED(entry.player());
                }
                break;
            case IPT_PEDAL:
                entry.defseq(SEQ_TYPE_STANDARD) |= JOYCODE_W_INDEXED(entry.player());
                entry.defseq(SEQ_TYPE_INCREMENT) |= JOYCODE_BUTTON1_INDEXED(entry.player());
                entry.defseq(SEQ_TYPE_INCREMENT) |= MOUSECODE_BUTTON1;
                break;
            case IPT_PEDAL2:
                entry.defseq(SEQ_TYPE_STANDARD) |= JOYCODE_Z_INDEXED(entry.player());
                entry.defseq(SEQ_TYPE_INCREMENT) |= JOYCODE_BUTTON2_INDEXED(entry.player());
                break;
            case IPT_POSITIONAL:
                entry.defseq(SEQ_TYPE_STANDARD) = input_code();
                //entry.defseq(SEQ_TYPE_INCREMENT) |= JOYCODE_BUTTON5_INDEXED(entry.player());
                //entry.defseq(SEQ_TYPE_DECREMENT) |= JOYCODE_BUTTON6_INDEXED(entry.player());
                break;
            case IPT_MOUSE_X:
				entry.defseq(SEQ_TYPE_STANDARD) |= JOYCODE_X_INDEXED(entry.player());
                entry.defseq(SEQ_TYPE_INCREMENT) |= JOYCODE_HAT1RIGHT_INDEXED(entry.player());
                entry.defseq(SEQ_TYPE_DECREMENT) |= JOYCODE_HAT1LEFT_INDEXED(entry.player());
				break;
            case IPT_MOUSE_Y:
				entry.defseq(SEQ_TYPE_STANDARD) |= JOYCODE_Y_INDEXED(entry.player());
                entry.defseq(SEQ_TYPE_INCREMENT) |= JOYCODE_HAT1DOWN_INDEXED(entry.player());
                entry.defseq(SEQ_TYPE_DECREMENT) |= JOYCODE_HAT1UP_INDEXED(entry.player());

                // leave everything else alone
            default:
                break;
        }
    }
}

//============================================================
//  osd_set_aggressive_input_focus
//============================================================

void osd_set_aggressive_input_focus(bool aggressive_focus)
{
    // dummy implementation for now?
}

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






