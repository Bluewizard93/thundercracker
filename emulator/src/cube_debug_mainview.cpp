/* -*- mode: C; c-basic-offset: 4; intent-tabs-mode: nil -*- */

/* 8051 emulator 
 *
 * Copyright 2006 Jari Komppa
 * Copyright <c> 2011 Sifteo, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining 
 * a copy of this software and associated documentation files (the 
 * "Software"), to deal in the Software without restriction, including 
 * without limitation the rights to use, copy, modify, merge, publish, 
 * distribute, sublicense, and/or sell copies of the Software, and to 
 * permit persons to whom the Software is furnished to do so, subject 
 * to the following conditions: 
 *
 * The above copyright notice and this permission notice shall be included 
 * in all copies or substantial portions of the Software. 
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS 
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, 
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE 
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER 
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING 
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS 
 * IN THE SOFTWARE. 
 *
 * mainview.c
 * Main view-related stuff for the curses-based emulator front-end
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include <curses.h>

#include "ostime.h"
#include "cube_debug.h"

namespace Cube {
namespace Debug {


/*
    The history-based display assumes that there's no
    self-modifying code. To get self-modifying code
    you'd have to do some rather creative wiring anyway,
    so I doubt this will be an issue. And the worst that
    can happen is that the disassembly looks wrong.
 */

// Last clock we updated the view
static unsigned int lastclock = 0;

// Memory editor mode
static int memmode = 0;

// focused component
static int focus = 0;

// memory window cursor position
static int memcursorpos = 0;

// cursor position for other windows
static int cursorpos = 0;

// memory window offset
static int memoffset = 0;

// pointer to the memory area being viewed
static unsigned char *memarea = NULL;

// code box (PC, opcode, assembly)
WINDOW *codebox = NULL, *codeoutput = NULL;

// Registers box (a, r0..7, b, dptr)
WINDOW *regbox = NULL, *regoutput = NULL;

// RAM view/edit box
WINDOW *rambox = NULL, *ramview = NULL;

// stack view
WINDOW *stackbox = NULL, *stackview = NULL;

// program status word box
WINDOW *pswbox = NULL, *pswoutput = NULL;

// IO registers box
WINDOW *ioregbox = NULL, *ioregoutput = NULL;

// special registers box
WINDOW *spregbox = NULL, *spregoutput = NULL;

// misc. stuff box
WINDOW *miscbox = NULL, *miscview = NULL;


const char *memtypes[]={"IDATA","SFR","XDATA","ROM"};
const char *regtypes[]={"     A ",
                        "    R0 ",
                        "    R1 ",
                        "    R2 ",
                        "    R3 ",
                        "    R4 ",
                        "    R5 ",
                        "    R6 ",
                        "    R7 ",
                        "     B ",
                        "   DPH ",
                        "   DPL "};

void wipe_main_view()
{
    delwin(codebox);
    delwin(codeoutput);
    delwin(regbox);
    delwin(regoutput);
    delwin(rambox);
    delwin(ramview);
    delwin(stackbox);
    delwin(stackview);
    delwin(pswbox);
    delwin(pswoutput);
    delwin(ioregbox);
    delwin(ioregoutput);
    delwin(spregbox);
    delwin(spregoutput);
    delwin(miscbox);
    delwin(miscview);
}

void build_main_view(CPU::em8051 *aCPU)
{
    erase();

    oldcols = COLS;
    oldrows = LINES;

    codebox = subwin(stdscr, LINES-17, 42, 17, 0);
    box(codebox,ACS_VLINE,ACS_HLINE);
    mvwaddstr(codebox, 0, 2, "PC");
    mvwaddstr(codebox, 0, 8, "Opcodes");
    mvwaddstr(codebox, 0, 18, "Assembly");
    mvwaddstr(codebox, LINES-19, 0, ">");
    mvwaddstr(codebox, LINES-19, 41, "<");
    codeoutput = subwin(codebox, LINES-19, 39, 18, 2);
    scrollok(codeoutput, TRUE);

    regbox = subwin(stdscr, LINES-17, 38, 17, 42);
    box(regbox,0,0);
    mvwaddstr(regbox, 0, 2, "A -R0-R1-R2-R3-R4-R5-R6-R7-B -DPTR");
    mvwaddstr(regbox, LINES-19, 0, ">");
    mvwaddstr(regbox, LINES-19, 37, "<");
    regoutput = subwin(regbox, LINES-19, 35, 18, 44);
    scrollok(regoutput, TRUE);

    rambox = subwin(stdscr, 10, 31, 0, 0);
    box(rambox,0,0);
    mvwaddstr(rambox, 0, 2, "m)");
    mvwaddstr(rambox, 0, 4, memtypes[memmode]);
    ramview = subwin(rambox, 8, 29, 1, 1);

    stackbox = subwin(stdscr, 17, 6, 0, 31);
    box(stackbox,0,0);
    mvwaddstr(stackbox, 0, 1, "Stck");
    mvwaddstr(stackbox, 8, 0, ">");
    mvwaddstr(stackbox, 8, 5, "<");
    stackview = subwin(stackbox, 15, 4, 1, 32);

    ioregbox = subwin(stdscr, 8, 24, 0, 37);
    box(ioregbox,0,0);
    mvwaddstr(ioregbox, 0, 2, "P0-P1-P2-P3-IEN---IR");
    mvwaddstr(ioregbox, 6, 0, ">");
    mvwaddstr(ioregbox, 6, 23, "<");
    ioregoutput = subwin(ioregbox, 6, 21, 1, 39);
    scrollok(ioregoutput, TRUE);

    pswbox = subwin(stdscr, 8, 19, 0, 61);
    box(pswbox,0,0);
    mvwaddstr(pswbox, 0, 2, "C-ACF0R1R0Ov--P");
    mvwaddstr(pswbox, 6, 0, ">");
    mvwaddstr(pswbox, 6, 18, "<");
    pswoutput = subwin(pswbox, 6, 16, 1, 63);
    scrollok(pswoutput, TRUE);

    spregbox = subwin(stdscr, 9, 43, 8, 37);
    box(spregbox,0,0);
    mvwaddstr(spregbox, 0, 2, "TMOD-TCON--TMR0--TMR1--TMR2--S0CON-P0CON");
    mvwaddstr(spregbox, 7, 0, ">");
    mvwaddstr(spregbox, 7, 42, "<");
    spregoutput = subwin(spregbox, 7, 40, 9, 39);
    scrollok(spregoutput, TRUE);

    miscbox = subwin(stdscr, 7, 31, 10, 0);
    box(miscbox,0,0);
    miscview = subwin(miscbox, 5, 28, 11, 2);
    
    refresh();
    wrefresh(codeoutput);
    wrefresh(regoutput);
    wrefresh(pswoutput);
    wrefresh(ioregoutput);
    wrefresh(spregoutput);

    lastclock = icount - 8;

    memarea = aCPU->mData;

}

int getregoutput(CPU::em8051 *aCPU, int pos)
{
    int rx = 8 * ((aCPU->mSFR[REG_PSW] & (PSWMASK_RS0|PSWMASK_RS1))>>PSW_RS0);
    switch (pos)
    {
    case 0:
        return aCPU->mSFR[REG_ACC];
    case 1:
        return aCPU->mData[rx + 0];
    case 2:
        return aCPU->mData[rx + 1];
    case 3:
        return aCPU->mData[rx + 2];
    case 4:
        return aCPU->mData[rx + 3];
    case 5:
        return aCPU->mData[rx + 4];
    case 6:
        return aCPU->mData[rx + 5];
    case 7:
        return aCPU->mData[rx + 6];
    case 8:
        return aCPU->mData[rx + 7];
    case 9:
        return aCPU->mSFR[REG_B];
    case 10:
        return aCPU->mSFR[CUR_DPH] << 8 | aCPU->mSFR[CUR_DPL];
    }
    return 0;
}

void setregoutput(CPU::em8051 *aCPU, int pos, int val)
{
    int rx = 8 * ((aCPU->mSFR[REG_PSW] & (PSWMASK_RS0|PSWMASK_RS1))>>PSW_RS0);
    switch (pos)
    {
    case 0:
        aCPU->mSFR[REG_ACC] = val;
        break;
    case 1:
        aCPU->mData[rx + 0] = val;
        break;
    case 2:
        aCPU->mData[rx + 1] = val;
        break;
    case 3:
        aCPU->mData[rx + 2] = val;
        break;
    case 4:
        aCPU->mData[rx + 3] = val;
        break;
    case 5:
        aCPU->mData[rx + 4] = val;
        break;
    case 6:
        aCPU->mData[rx + 5] = val;
        break;
    case 7:
        aCPU->mData[rx + 6] = val;
        break;
    case 8:
        aCPU->mData[rx + 7] = val;
        break;
    case 9:
        aCPU->mSFR[REG_B] = val;
        break;
    case 10:
        aCPU->mSFR[CUR_DPH] = (val >> 8) & 0xff;
        aCPU->mSFR[CUR_DPL] = val & 0xff;
        break;
    }
}


void mainview_editor_keys(CPU::em8051 *aCPU, int ch)
{
    int insert_value = -1;
    int maxmem = 0;

    switch(ch)
    {
    case KEY_NEXT:
    case '\t':
        cursorpos = 0;
        focus++;
        if (focus == 2)
            focus = 0;
        break;
    case 'm':
    case 'M':
        memcursorpos = 0;
        memoffset = 0;
        memmode++;
        if (memmode == 4)
            memmode = 0;
        switch (memmode)
        {
        case 0:
            memarea = aCPU->mData;
            break;
        case 1:
            memarea = aCPU->mSFR;
            break;
        case 2:
            memarea = aCPU->mExtData;
            break;
        case 3:
            memarea = aCPU->mCodeMem;
            break;
        }
        mvwaddstr(rambox, 0, 4, memtypes[memmode]);
        wrefresh(rambox);
        break;
    case KEY_NPAGE:
        memcursorpos += 8 * 16;
        break;
    case KEY_PPAGE:
        memcursorpos -= 8 * 16;
        break;
    case KEY_RIGHT:
        if (focus == 0)
            memcursorpos++;
        cursorpos++;
        if (focus == 1 && cursorpos > 23)
            cursorpos = 23;
        break;
    case KEY_LEFT:
        if (focus == 0)
            memcursorpos--;
        cursorpos--;
        if (cursorpos < 0)
            cursorpos = 0;
        break;
    case KEY_UP:
        if (focus == 0)
            memcursorpos-=16;
        break;
    case KEY_DOWN:
        if (focus == 0)
            memcursorpos+=16;
        break;
    case '0':
    case '1':
    case '2':
    case '3':
    case '4':
    case '5':
    case '6':
    case '7':
    case '8':
    case '9':
        insert_value = ch - '0';
        break;
    case 'a':
    case 'A':
        insert_value = 0xa;
        break;
    case 'b':
    case 'B':
        insert_value = 0xb;
        break;
    case 'c':
    case 'C':
        insert_value = 0xc;
        break;
    case 'd':
    case 'D':
        insert_value = 0xd;
        break;
    case 'e':
    case 'E':
        insert_value = 0xe;
        break;
    case 'f':
    case 'F':
        insert_value = 0xf;
        break;
    }

    switch (memmode)
    {
    case 0:
        maxmem = 256;
        break;
    case 1:
        maxmem = 128;
        break;
    case 2:
        maxmem = XDATA_SIZE;
        break;
    case 3:
        maxmem = CODE_SIZE;
        break;
    }

    if (insert_value != -1)
    {
        if (focus == 0)
        {
            if (memcursorpos & 1)
                memarea[memoffset + (memcursorpos / 2)] = (memarea[memoffset + (memcursorpos / 2)] & 0xf0) | insert_value;
            else
                memarea[memoffset + (memcursorpos / 2)] = (memarea[memoffset + (memcursorpos / 2)] & 0x0f) | (insert_value << 4);
            memcursorpos++;
        }
        if (focus == 1)
        {
            if (cursorpos / 2 >= 10)
            {
                int oldvalue = getregoutput(aCPU, 10);
                setregoutput(aCPU, 10, (oldvalue & ~(0xf000 >> (4 * (cursorpos & 3)))) | (insert_value << (4 * (3 - (cursorpos & 3)))));
            }
            else
            {
                if (cursorpos & 1)
                    setregoutput(aCPU, cursorpos / 2, (getregoutput(aCPU, cursorpos / 2) & 0xf0) | insert_value);
                else
                    setregoutput(aCPU, cursorpos / 2, (getregoutput(aCPU, cursorpos / 2) & 0x0f) | (insert_value << 4));
            }
            cursorpos++;
            if (cursorpos > 23)
                cursorpos = 23;
        }
    }

    while (memcursorpos < 0)
    {
        memoffset -= 8;
        if (memoffset < 0)
        {
            memoffset = 0;
            memcursorpos = 0;
        }
        else
        {
            memcursorpos += 16;
        }
    }
    while (memcursorpos > 128 - 1)
    {
        memoffset += 8;
        if (memoffset > (maxmem - 8*8))
        {
            memoffset = (maxmem - 8*8);
            memcursorpos = 128 - 1;
        }
        else
        {
            memcursorpos -= 16;
        }
    }
}


void refresh_regoutput(CPU::em8051 *aCPU, int cursor)
{
    int rx = 8 * ((aCPU->mSFR[REG_PSW] & (PSWMASK_RS0|PSWMASK_RS1))>>PSW_RS0);
    
    mvwprintw(regoutput, LINES-19, 0, "%02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %04X",
        aCPU->mSFR[REG_ACC],
        aCPU->mData[0 + rx],
        aCPU->mData[1 + rx],
        aCPU->mData[2 + rx],
        aCPU->mData[3 + rx],
        aCPU->mData[4 + rx],
        aCPU->mData[5 + rx],
        aCPU->mData[6 + rx],
        aCPU->mData[7 + rx],
        aCPU->mSFR[REG_B],
        (aCPU->mSFR[CUR_DPH]<<8)|aCPU->mSFR[CUR_DPL]);

    if (focus == 1 && cursor)
    {   
        int bytevalue = getregoutput(aCPU, cursorpos / 2);
        if (cursorpos / 2 == 10)
            bytevalue = getregoutput(aCPU, 10) >> 8;
        if (cursorpos / 2 == 11)
            bytevalue = getregoutput(aCPU, 10) & 0xff;
        wattron(regoutput, A_REVERSE);
        wmove(regoutput, LINES-19, (cursorpos / 2) * 3 + (cursorpos & 1) - (cursorpos / 22));
        wprintw(regoutput,"%X", (bytevalue >> (4 * (!(cursorpos & 1)))) & 0xf);
        wattroff(regoutput, A_REVERSE); 
    }
}

void mainview_update(Cube::Hardware *cube)
{
    CPU::em8051 *aCPU = &cube->cpu;
    int bytevalue;
    int i;

    int opcode_bytes;
    int stringpos;
    int rx;
    unsigned int hline;

    if ((speed != 0 || !runmode) && lastclock != icount)
    {
        // make sure we only display HISTORY_LINES worth of data
        if (icount - lastclock > HISTORY_LINES)
            lastclock = icount - HISTORY_LINES;

        // + HISTORY_LINES to force positive result
        hline = historyline - (icount - lastclock) + HISTORY_LINES;

        while (lastclock != icount)
        {
            char assembly[128];
            char temp[256];
            int old_pc;
            int hoffs;
            
            hline = (hline + 1) % HISTORY_LINES;

            hoffs = (hline * (128 + 64 + sizeof(int)));

            memcpy(&old_pc, history + hoffs + 128 + 64, sizeof(int));
            opcode_bytes = CPU::em8051_decode(aCPU, old_pc, assembly);
            stringpos = 0;
            stringpos += sprintf(temp + stringpos,"\n%04X  ", old_pc & 0xffff);
            
            for (i = 0; i < opcode_bytes; i++)
                stringpos += sprintf(temp + stringpos,"%02X ", aCPU->mCodeMem[(old_pc + i) & PC_MASK]);
            
            for (i = opcode_bytes; i < 3; i++)
                stringpos += sprintf(temp + stringpos,"   ");

            sprintf(temp + stringpos," %s",assembly);                    

            wprintw(codeoutput, "%s", temp);

            rx = 8 * ((history[hoffs + REG_PSW] & (PSWMASK_RS0|PSWMASK_RS1))>>PSW_RS0);
            
            sprintf(temp, "\n%02X %02X %02X %02X %02X %02X %02X %02X %02X %02X %04X",
                history[hoffs + REG_ACC],
                history[hoffs + 128 + 0 + rx],
                history[hoffs + 128 + 1 + rx],
                history[hoffs + 128 + 2 + rx],
                history[hoffs + 128 + 3 + rx],
                history[hoffs + 128 + 4 + rx],
                history[hoffs + 128 + 5 + rx],
                history[hoffs + 128 + 6 + rx],
                history[hoffs + 128 + 7 + rx],
                history[hoffs + REG_B],
                    (history[hoffs + SEL_DPH(history[hoffs + REG_DPS])]<<8) |
                     history[hoffs + SEL_DPL(history[hoffs + REG_DPS])]);
            if (focus == 1)
                refresh_regoutput(aCPU, 0);
            wprintw(regoutput,"%s",temp);

            sprintf(temp, "\n%d %d %d %d %d %d %d %d",
                (history[hoffs + REG_PSW] >> 7) & 1,
                (history[hoffs + REG_PSW] >> 6) & 1,
                (history[hoffs + REG_PSW] >> 5) & 1,
                (history[hoffs + REG_PSW] >> 4) & 1,
                (history[hoffs + REG_PSW] >> 3) & 1,
                (history[hoffs + REG_PSW] >> 2) & 1,
                (history[hoffs + REG_PSW] >> 1) & 1,
                (history[hoffs + REG_PSW] >> 0) & 1);
            wprintw(pswoutput,"%s",temp);

            sprintf(temp, "\n%02X %02X %02X %02X %02X-%02X %02X",
                    history[hoffs + REG_P0],
                    history[hoffs + REG_P1],
                    history[hoffs + REG_P2],
                    history[hoffs + REG_P3],
                    history[hoffs + REG_IEN0],
                    history[hoffs + REG_IEN1],
                    history[hoffs + REG_IRCON]);
            wprintw(ioregoutput,"%s",temp);

            sprintf(temp, "\n%02X   %02X    %02X%02X  %02X%02X  %02X%02X   %02X   %02X",
                history[hoffs + REG_TMOD],
                history[hoffs + REG_TCON],
                history[hoffs + REG_TH0],
                history[hoffs + REG_TL0],
                history[hoffs + REG_TH1],
                history[hoffs + REG_TL1],
                history[hoffs + REG_TH2],
                history[hoffs + REG_TL2],
                history[hoffs + REG_S0CON],
                history[hoffs + REG_P0CON]);
            wprintw(spregoutput, "%s", temp);

            lastclock++;
        }
    }

    {
        /*
         * XXX: Needs some refactoring bigtime! There are a lot of utilities
         *      in vtime.h now which abstract the bulk of this work.
         */
         
        unsigned int update_interval = VirtualTime::HZ;
        static uint64_t update_prev_clocks = 0;
        static double update_prev_time = 0;
        static uint32_t prev_lcd_fr;
        
        static float lcd_fps = 0;
        static float clock_ratio = 0;
        static float radio_b = 0;
        static float radio_rx = 0;
        static float flash_hz = 0;
        static unsigned flash_percent = 0;

        enum Flash::busy_flag flash_busy = cube->flash.getBusyFlag();

        float msec = 1000.0f * cube->time->elapsedSeconds();
        float clock_mhz =  cube->time->clockMHZ();

        /* Periodically update most of the stats */
        if (cube->time->clocks < update_prev_clocks ||
            (cube->time->clocks - update_prev_clocks) > update_interval) {
            double now = OSTime::clock();

            if (cube->time->clocks > update_prev_clocks && now > update_prev_time) {                
                float virtual_elapsed = VirtualTime::toSeconds(cube->time->clocks - update_prev_clocks);
                float real_elapsed = now - update_prev_time;

                uint32_t lcd_fr = cube->lcd.getFrameCount();
                lcd_fps = (lcd_fr - prev_lcd_fr) / virtual_elapsed;
                prev_lcd_fr = lcd_fr;
                
                radio_b = cube->spi.radio.getByteCount() / virtual_elapsed;
                radio_rx = cube->spi.radio.getRXCount() / virtual_elapsed;
                flash_hz = cube->flash.getCycleCount() / virtual_elapsed;
                clock_ratio = virtual_elapsed / real_elapsed;
                flash_percent = cube->flash.getBusyPercent();
            } else {
                lcd_fps = 0;
                radio_b = 0;
                radio_rx = 0;
                clock_ratio = 0;
                flash_hz = 0;
                flash_percent = 0;
            }

            update_prev_time = now;
            update_prev_clocks = cube->time->clocks;
        }

        werase(miscview);
        wprintw(miscview, "LCD    : ");
        wattron(miscview, A_REVERSE);
        wprintw(miscview, "% 8.3f FPS \n", lcd_fps);
        wattroff(miscview, A_REVERSE);

        wprintw(miscview, "Flash  :% 7.3f MHz %c%c % 3u%%\n", flash_hz / 1000000.0,
                flash_busy & Flash::BF_PROGRAM ? 'W' : '-',
                flash_busy & Flash::BF_ERASE   ? 'E' : '-',
                flash_percent);

        wprintw(miscview, "Radio  :% 5d RX% 6.2f kB/s\n", (int)radio_rx, radio_b / 1000);
        wprintw(miscview, "Time   : %07.2f ms %04llu ck\n", fmod(msec, 10000.0), cube->time->clocks % 10000);
        wprintw(miscview, "Speed  :% 6.1f%% %0.1f MHz\n", clock_ratio * 100, clock_mhz);
    }

    werase(ramview);
    for (i = 0; i < 8; i++)
    {
        wprintw(ramview,"%04X %02X %02X %02X %02X %02X %02X %02X %02X\n", 
            i*8+memoffset, 
            memarea[i*8+0+memoffset], memarea[i*8+1+memoffset], memarea[i*8+2+memoffset], memarea[i*8+3+memoffset],
            memarea[i*8+4+memoffset], memarea[i*8+5+memoffset], memarea[i*8+6+memoffset], memarea[i*8+7+memoffset]);
    }

    if (focus == 0)
    {
        bytevalue = memarea[memcursorpos / 2 + memoffset];
        wattron(ramview, A_REVERSE);
        wmove(ramview, memcursorpos / 16, 5 + ((memcursorpos % 16) / 2) * 3 + (memcursorpos & 1));
        wprintw(ramview,"%X", (bytevalue >> (4 * (!(memcursorpos & 1)))) & 0xf);
        wattroff(ramview, A_REVERSE);
    }

    refresh_regoutput(aCPU, 1);

    for (i = 0; i < 13; i++)
    {
        int offset = (i + aCPU->mSFR[REG_SP]-7)&0xff;
        wprintw(stackview," %02X\n", aCPU->mData[offset]);
    }
    wprintw(stackview,"Max\n %02X\n", stackMax);    

    if (speed != 0 || runmode == 0)
    {
        wrefresh(ramview);
        wrefresh(stackview);
    }
    werase(stackview);
    wrefresh(miscview);
    if (speed != 0 || runmode == 0)
    {
        wrefresh(codeoutput);
        wrefresh(regoutput);
        wrefresh(ioregoutput);
        wrefresh(spregoutput);
        wrefresh(pswoutput);
    }
}


};  // namespace Debug
};  // namespace Cube
