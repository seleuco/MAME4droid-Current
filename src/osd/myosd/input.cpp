// license:BSD-3-Clause
//============================================================
//
//  input.cpp - osd input handling: the single host poll
//  (netplay interception point) and the input profile
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

// include this to get mame_ui_manager, sigh
//#include "../frontend/mame/ui/ui.h"


// MAMEOS headers
#include "myosd.h"
#include "netplay.h"
#include "myosd_netplay.h"

//============================================================
// global input state
//============================================================
/* Non-static: myosd_netplay.cpp reads/writes it too (see extern
 * declaration in myosd_netplay.h). */
myosd_input_state g_input;

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
        int joy_h = 0, joy_v = 0;
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

                // count buttons from the ACTUAL assigned sequences, not the raw
                // field type: asteroids maps hyperspace to IPT_BUTTON5 but its
                // PORT_CODE seq is JOYCODE_BUTTON3, so only 3 buttons are reachable
                if ((field.type() >= IPT_BUTTON1 && field.type() <= IPT_BUTTON12) ||
                    (field.type() >= IPT_DIGITAL_JOYSTICK_FIRST && field.type() <= IPT_DIGITAL_JOYSTICK_LAST))
                {
                    input_seq const seq = field.seq(SEQ_TYPE_STANDARD);
                    for (int i=0; seq[i] != input_seq::end_code; i++)
                    {
                        input_code code = seq[i];
                        if (code.device_class() == DEVICE_CLASS_JOYSTICK &&
                            code.item_id() >= ITEM_ID_BUTTON1 && code.item_id() <= ITEM_ID_BUTTON6 &&
                            g_input.num_buttons < (code.item_id() - ITEM_ID_BUTTON1 + 1))
                            g_input.num_buttons = code.item_id() - ITEM_ID_BUTTON1 + 1;
                    }
                }
                if(field.type() == IPT_POSITIONAL)//positional is mapped with two last buttons
                    if(g_input.num_buttons<6)g_input.num_buttons=6;

                // count the number of ways (joystick directions)
                if(field.type() == IPT_JOYSTICK_UP || field.type() == IPT_JOYSTICK_DOWN || field.type() == IPT_JOYSTICKLEFT_UP || field.type() == IPT_JOYSTICKLEFT_DOWN)
                    way8=1;
                if(field.type() == IPT_AD_STICK_X || field.type() == IPT_LIGHTGUN_X || field.type() == IPT_MOUSE_X ||
                   field.type() == IPT_TRACKBALL_X || field.type() == IPT_PEDAL)
                    way8=1;

                // full-stick detection (used below to veto the touch mouse);
                // a lone 2-way like a gear shifter must not count as a stick
                switch (field.type())
                {
                    case IPT_JOYSTICK_UP: case IPT_JOYSTICK_DOWN:
                    case IPT_JOYSTICKLEFT_UP: case IPT_JOYSTICKLEFT_DOWN:
                    case IPT_JOYSTICKRIGHT_UP: case IPT_JOYSTICKRIGHT_DOWN:
                        joy_v = 1; break;
                    case IPT_JOYSTICK_LEFT: case IPT_JOYSTICK_RIGHT:
                    case IPT_JOYSTICKLEFT_LEFT: case IPT_JOYSTICKLEFT_RIGHT:
                    case IPT_JOYSTICKRIGHT_LEFT: case IPT_JOYSTICKRIGHT_RIGHT:
                        joy_h = 1; break;
                    default: break;
                }

                // touch mouse candidates come from the analog seq scan above
                // (DIAL/PADDLE/TRACKBALL/AD_STICK defaults all carry MOUSECODEs)
                if(g_input.num_lightgun == 0 && field.type() == IPT_LIGHTGUN_X)
                    g_input.num_lightgun = 1;
                if(field.type() == IPT_KEYBOARD)
                    g_input.num_keyboard = 1;
            }
        }

        if(g_input.num_keyboard)
            g_input.num_mouse = 0;//no queremos raton touch en micro

        // a real stick (H+V) on P1 means the game is joystick-driven and any
        // analog input is secondary (gauntleg 49-way, taito_f3 shared dial,
        // tron spinner): when in doubt, buttons win over the touch mouse
        if (joy_h && joy_v)
            g_input.num_mouse = 0;

        // exceptions: f3 paddle games where the shared dial IS the main control
        if (std::string(machine.system().type.source()).find("taito_f3") != std::string::npos &&
            (std::string(machine.system().name).find("arkretrn") != std::string::npos ||
             std::string(machine.system().name).find("puchicar") != std::string::npos))
            g_input.num_mouse = 1;

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
    static bool last_relative_reset = false;
    bool is_new_frame = (relative_reset == true && last_relative_reset == false);
    last_relative_reset = relative_reset;

    osd_printf_verbose("my_osd_interface::input_update\n");

    // fill in the input profile the first time
    if (g_input.num_ways == 0) {

        if (!machine().ioport().safe_to_read())
            return;

        input_profile_init(machine());

        if (m_callbacks.input_init != nullptr)
            m_callbacks.input_init(&g_input, sizeof(g_input));
    }

    bool is_paused = myosd_is_paused();
    bool is_empty_game = (strcmp(machine().system().name, "___empty") == 0);
    bool netplay_in_game = (machine().phase() == machine_phase::RUNNING) && !is_empty_game;

    //bool in_menu = (machine().phase() != machine_phase::RUNNING) || machine().ui().is_menu_active() || is_empty_game  || machine().ui().is_general_active(); 
    bool in_menu = machine().phase() == machine_phase::RUNNING && machine().ui().is_menu_active();

    // determine if we should disable the rest of the UI
    bool has_keyboard = g_input.num_keyboard != 0; // machine_info().has_keyboard();

    //mame_ui_manager *ui = dynamic_cast<mame_ui_manager *>(&machine.ui());
    //bool ui_disabled = (has_keyboard && !ui->ui_active());
    bool ui_disabled = (has_keyboard && false);

    // set the current input mode
    g_input.input_mode = in_menu ? MYOSD_INPUT_MODE_MENU : (ui_disabled ? MYOSD_INPUT_MODE_KEYBOARD : MYOSD_INPUT_MODE_NORMAL);

    netplay_t *handle = netplay_get_handle();

    // and call host
    if (m_callbacks.input_poll != nullptr)
        m_callbacks.input_poll(relative_reset,&g_input, sizeof(g_input));

    if (netplay_in_game) {
        if (myosd_netplay_input_update(handle, is_new_frame, is_paused))
            return;
    }

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
            //button 1 of the arcade sits on the right pad button, so the bottom
            //one arrives as button 2. Bind accept/back there to match Android.
            case IPT_UI_SELECT://DAV
                entry.defseq(SEQ_TYPE_STANDARD) |= JOYCODE_BUTTON2;
                break;
            case IPT_UI_BACK://DAV
                entry.defseq(SEQ_TYPE_STANDARD) |= JOYCODE_BUTTON1;
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
                entry.defseq(SEQ_TYPE_STANDARD) |= (JOYCODE_SELECT);
                entry.defseq(SEQ_TYPE_STANDARD) += JOYCODE_Y_UP_SWITCH_INDEXED(0);
                break;
            case IPT_COIN3:
                entry.defseq(SEQ_TYPE_STANDARD) |= (JOYCODE_SELECT);
                entry.defseq(SEQ_TYPE_STANDARD) += JOYCODE_HAT1RIGHT_INDEXED(0);
                entry.defseq(SEQ_TYPE_STANDARD) |= (JOYCODE_SELECT);
                entry.defseq(SEQ_TYPE_STANDARD) += JOYCODE_X_RIGHT_SWITCH_INDEXED(0);
                break;
            case IPT_COIN4:
                entry.defseq(SEQ_TYPE_STANDARD) |= (JOYCODE_SELECT);
                entry.defseq(SEQ_TYPE_STANDARD) += JOYCODE_HAT1DOWN_INDEXED(0);
                entry.defseq(SEQ_TYPE_STANDARD) |= (JOYCODE_SELECT);
                entry.defseq(SEQ_TYPE_STANDARD) += JOYCODE_Y_DOWN_SWITCH_INDEXED(0);
                break;

            //START+UP/RIGHT/DOWN starts 2P/3P/4P game
            case IPT_START2:
                entry.defseq(SEQ_TYPE_STANDARD) |= (JOYCODE_START);
                entry.defseq(SEQ_TYPE_STANDARD) += JOYCODE_HAT1UP_INDEXED(0);
                entry.defseq(SEQ_TYPE_STANDARD) |= (JOYCODE_START);
                entry.defseq(SEQ_TYPE_STANDARD) += JOYCODE_Y_UP_SWITCH_INDEXED(0);
                break;
            case IPT_START3:
                entry.defseq(SEQ_TYPE_STANDARD) |= (JOYCODE_START);
                entry.defseq(SEQ_TYPE_STANDARD) += JOYCODE_HAT1RIGHT_INDEXED(0);
                entry.defseq(SEQ_TYPE_STANDARD) |= (JOYCODE_START);
                entry.defseq(SEQ_TYPE_STANDARD) += JOYCODE_X_RIGHT_SWITCH_INDEXED(0);
                break;
            case IPT_START4:
                entry.defseq(SEQ_TYPE_STANDARD) |= (JOYCODE_START);
                entry.defseq(SEQ_TYPE_STANDARD) += JOYCODE_HAT1DOWN_INDEXED(0);
                entry.defseq(SEQ_TYPE_STANDARD) |= (JOYCODE_START);
                entry.defseq(SEQ_TYPE_STANDARD) += JOYCODE_Y_DOWN_SWITCH_INDEXED(0);
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
            case IPT_UI_PAGE_DOWN:
                entry.defseq(SEQ_TYPE_STANDARD) |= JOYCODE_BUTTON5;

                entry.defseq(SEQ_TYPE_STANDARD) |= (JOYCODE_BUTTON1);
                entry.defseq(SEQ_TYPE_STANDARD) += JOYCODE_HAT1DOWN_INDEXED(0);

                entry.defseq(SEQ_TYPE_STANDARD) |= (JOYCODE_BUTTON1);
                entry.defseq(SEQ_TYPE_STANDARD) += JOYCODE_Y_DOWN_SWITCH_INDEXED(0);
                break;

            case IPT_UI_PAGE_UP:
                entry.defseq(SEQ_TYPE_STANDARD) |= JOYCODE_BUTTON6;

                entry.defseq(SEQ_TYPE_STANDARD) |= (JOYCODE_BUTTON1);
                entry.defseq(SEQ_TYPE_STANDARD) += JOYCODE_HAT1UP_INDEXED(0);

                entry.defseq(SEQ_TYPE_STANDARD) |= (JOYCODE_BUTTON1);
                entry.defseq(SEQ_TYPE_STANDARD) += JOYCODE_Y_UP_SWITCH_INDEXED(0);
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

