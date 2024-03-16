/* Copyright (c) 2021-2024
*
* This program is free software; you can redistribute it and/or
* modify it under the terms of the GNU General Public
* License as published by the Free Software Foundation; either
* version 2 of the License, or (at your option) any later version.
#
* This program is distributed in the hope that it will be useful,
* but WITHOUT ANY WARRANTY; without even the implied warranty of
* MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
* General Public License for more details.
#
* You should have received a copy of the GNU General Public
* License along with this program; if not, write to the
* Free Software Foundation, Inc., 51 Franklin Street, Fifth Floor,
* Boston, MA 02110-1301 USA
#
* Authored by: Kris Henriksen <krishenriksen.work@gmail.com>
#
* AnberPorts-Keyboard-Mouse
* 
* Part of the code is from from https://github.com/krishenriksen/AnberPorts/blob/master/AnberPorts-Keyboard-Mouse/main.c (mostly the fake keyboard)
* Fake Xbox code from: https://github.com/Emanem/js2xbox
* 
* Modified (badly) by: Shanti Gilbert for EmuELEC
* Modified further by: Nikolai Wuttke for EmuELEC (Added support for SDL and the SDLGameControllerdb.txt)
* Modified further by: Jacob Smith
* 
* Any help improving this code would be greatly appreciated! 
* 
* DONE: Xbox360 mode: Fix triggers so that they report from 0 to 255 like real Xbox triggers
*       Xbox360 mode: Figure out why the axis are not correctly labeled?  SDL_CONTROLLER_AXIS_RIGHTX / SDL_CONTROLLER_AXIS_RIGHTY / SDL_CONTROLLER_AXIS_TRIGGERLEFT / SDL_CONTROLLER_AXIS_TRIGGERRIGHT
*       Keyboard mode: Add a config file option to load mappings from.
*       add L2/R2 triggers
* 
*/

#include "gptokeyb2.h"

#define MAX_PROCESS_NAME 64
#define DEFAULT_CONFIG "~/.config/gptokeyb2.ini"

int uinp_fd=0;
bool xbox360_mode=false;
bool config_mode=false;

bool want_pc_quit = false;
bool want_kill = false;
bool want_sudo = false;

char kill_process_name[64] = "";


gptokeyb_config *default_config=NULL;

struct uinput_user_dev uidev;


int main(int argc, char* argv[])
{
    bool do_dump_config = false;

    state_init();
    config_init();

    // Add hotkey environment variable if available
    char* env_hotkey = SDL_getenv("HOTKEY");
    if (env_hotkey)
    {
        const button_match *button = find_button(env_hotkey);

        if (button != NULL)
        {
            printf("set hotkey as %s\n", env_hotkey);
            set_hotkey(button->gbtn);
        }
    }

    // Add pc alt+f4 exit environment variable if available
    char* env_pckill_mode = SDL_getenv("PCKILLMODE");
    if (env_pckill_mode)
    {
        if (strcmp(env_pckill_mode, "Y") == 0)
        {
            printf("Using pc quit mode.\n");
            want_pc_quit = true;
        }
    }

    char* pkill_mode = SDL_getenv("NO_PKILL");
    if (pkill_mode)
    {
        want_kill = true;
    }

    int opt;
    char default_control[MAX_CONTROL_NAME] = "";

    while ((opt = getopt(argc, argv, "hdxp:c:XPH:")) != -1)
    {
        switch (opt)
        {
        case 'X':
            if (!want_kill)
            {            
                printf("Using kill mode.\n");
                want_kill = true;
            }
            break;

        case 'Z':
            if (want_kill)
            {            
                printf("Using pkill mode.\n");
                want_kill = false;
            }
            break;

        case 'P':
            if (!want_pc_quit)
            {            
                printf("Using pc quit mode.\n");
                want_pc_quit = true;
            }
            break;

        case 'H':
            {
                const button_match *button = find_button(optarg);

                if (button != NULL)
                {
                    printf("set hotkey as %s\n", optarg);
                    set_hotkey(button->gbtn);
                }
                else
                {
                    printf("unable to set hotkey as %s, unknown hotkey\n", optarg);
                }
            }
            break;

        case 'd':
            do_dump_config = true;
            break;

        case 'c':
            config_load(optarg, false);
            config_mode = true;
            xbox360_mode = false;
            break;

        case 'x':
            config_mode = false;
            xbox360_mode = true;
            break;

        case 'p':
            strncpy(default_control, optarg, MAX_CONTROL_NAME-1);
            printf("using control %s\n", default_control);
            break;

        case '?':
            if (optopt == 'c')
                fprintf (stderr, "Option -%c requires an argument.\n", optopt);
            else if (isprint (optopt))
                fprintf (stderr, "Unknown option `-%c'.\n", optopt);
            else
                fprintf (stderr, "Unknown option character `\\x%x'.\n", optopt);

            fprintf(stderr, "\n");

        case 'h':
            fprintf(stderr, "Usage: %s <program> [-dPXZ] [-H hotkey] [-c <config.ini>] [-p control_mode]\n",
                argv[0]);
            fprintf(stderr, "\n");
            fprintf(stderr, "Args:\n");
            fprintf(stderr, "  -P                  - pc quit mode (sends alt + f4 to quit program)\n");
            fprintf(stderr, "  -X                  - uses kill to quit the program\n");
            fprintf(stderr, "  -Z                  - uses pkill to quit the program\n");
            fprintf(stderr, "\n");
            fprintf(stderr, "  -x                  - xbox360 mode.\n");
            fprintf(stderr, "  -c  \"config.ini\"    - config file to load.\n");
            fprintf(stderr, "  -p  \"control\"       - what control mode to start in.\n");
            fprintf(stderr, "\n");
            fprintf(stderr, "  -d                  - dump config parsed.\n");
            fprintf(stderr, "\n");
            break;

        default:
            config_quit();
            exit(EXIT_FAILURE);
            break;
        }
    }

    for (int index=optind, i=0; index < argc; index++, i++)
    {
        if (i == 0)
        {
            strncpy(kill_process_name, argv[index], MAX_PROCESS_NAME);
        }
        else
        {
            printf("Extra option: %s\n", argv[index]);
        }
    }

    if (config_mode)
    {
        if (access(DEFAULT_CONFIG, F_OK) == 0)
        {
            printf("Loading '%s'\n", DEFAULT_CONFIG);

            if (config_load(DEFAULT_CONFIG, true))
            {
                config_quit();
                return 1;
            }
        }

        if (strcmp(default_control, "") != 0)
        {
            default_config = config_find(default_control);
        }

        if (default_config == NULL)
        {
            default_config = root_config;
        }

        config_stack[0] = default_config;
    }

    config_finalise();

    if (do_dump_config)
        config_dump();

    // SDL initialization and main loop
    if (SDL_Init(SDL_INIT_GAMECONTROLLER | SDL_INIT_TIMER) != 0)
    {
        printf("SDL_Init() failed: %s\n", SDL_GetError());
        return -1;
    }

    // Create fake input device (not needed in kill mode)
    //if (!kill_mode) {  
    if (config_mode || xbox360_mode)
    {   // initialise device, even in kill mode, now that kill mode will work with config & xbox modes
        uinp_fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK);

        if (uinp_fd < 0)
        {
            printf("Unable to open /dev/uinput\n");
            return -1;
        }

        // Intialize the uInput device to NULL
        memset(&uidev, 0, sizeof(uidev));
        uidev.id.version = 1;
        uidev.id.bustype = BUS_USB;

        if (xbox360_mode)
        {
            printf("Running in Fake Xbox 360 Mode\n");
            setupFakeXbox360Device(&uidev, uinp_fd);
        }
        else
        {
            printf("Running in Fake Keyboard mode\n");
            setupFakeKeyboardMouseDevice(&uidev, uinp_fd);
        }

        // Create input device into input sub-system
        write(uinp_fd, &uidev, sizeof(uidev));

        if (ioctl(uinp_fd, UI_DEV_CREATE)) {
            printf("Unable to create UINPUT device.");
            return -1;
        }
    }

    const char* db_file = SDL_getenv("SDL_GAMECONTROLLERCONFIG_FILE");
    if (db_file)
    {
        SDL_GameControllerAddMappingsFromFile(db_file);
    }

    SDL_Event event;

    while (current_state.running)
    {
        // POLL events
        while (current_state.running && SDL_PollEvent(&event))
        {
            handleInputEvent(&event);
        }

        state_update();

        // sleep.
        SDL_Delay(16);
    }

    SDL_Quit();

    /*
        * Give userspace some time to read the events before we destroy the
        * device with UI_DEV_DESTROY.
        */
    sleep(1);

    /* Clean up */
    ioctl(uinp_fd, UI_DEV_DESTROY);
    close(uinp_fd);

    return 0;
}
