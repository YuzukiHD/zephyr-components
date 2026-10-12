/* gameplaySP
 *
 * Copyright (C) 2006 Exophase <exophase@gmail.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of
 * the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#include "common.h"

unsigned turbo_period      = TURBO_PERIOD_MIN;
unsigned turbo_pulse_width = TURBO_PULSE_WIDTH_MIN;
unsigned turbo_a_counter   = 0;
unsigned turbo_b_counter   = 0;

static u32 old_key = 0;
static u32 held_keys = 0;

void set_input_keys(u32 keys)
{
  held_keys = keys;
}

/* A key went down: raise the keypad interrupt when the game asked for it (KEYCNT) */
static void trigger_key(u32 key)
{
  u32 p1_cnt = read_ioreg(REG_P1CNT);

  if((p1_cnt >> 14) & 0x01)
  {
    u32 key_intersection = (p1_cnt & key) & 0x3FF;

    if(p1_cnt >> 15)
    {
      /* AND: all the keys selected */
      if(key_intersection == (p1_cnt & 0x3FF))
      {
        flag_interrupt(IRQ_KEYPAD);
        check_and_raise_interrupts();
      }
    }
    else
    {
      /* OR: any of the keys selected */
      if(key_intersection)
      {
        flag_interrupt(IRQ_KEYPAD);
        check_and_raise_interrupts();
      }
    }
  }
}

u32 update_input(void)
{
   u32 new_key = held_keys & 0x3FF;

   /* Handle turbo buttons */
   if (held_keys & BUTTON_TURBO_A)
   {
      new_key |= (turbo_a_counter < turbo_pulse_width) ? BUTTON_A : 0;

      turbo_a_counter++;
      if (turbo_a_counter >= turbo_period)
         turbo_a_counter = 0;
   }
   else
      turbo_a_counter = 0;

   if (held_keys & BUTTON_TURBO_B)
   {
      new_key |= (turbo_b_counter < turbo_pulse_width) ? BUTTON_B : 0;

      turbo_b_counter++;
      if (turbo_b_counter >= turbo_period)
         turbo_b_counter = 0;
   }
   else
      turbo_b_counter = 0;

   // GBP keypad detection hack (only at game startup!)
   if (serial_mode == SERIAL_MODE_GBP) {
     // During the startup screen (aproximate)
     if (frame_counter > 20 && frame_counter < 100) {
       // Emulate 4 keypad buttons pressed (which is impossible).
       new_key = (frame_counter % 3) ? 0x3FF : 0x30F;
     }
   }

   if ((new_key | old_key) != old_key)
      trigger_key(new_key);

   old_key = new_key;
   /* Only the low 10 bits of KEYINPUT are keys, a pressed key reads 0 */
   write_ioreg(REG_P1, (~old_key) & 0x3FF);

   return 0;
}

bool input_check_savestate(const u8 *src)
{
  const u8 *p = bson_find_key(src, "input");
  return (p && bson_contains_key(p, "prevkey", BSON_TYPE_INT32));
}

bool input_read_savestate(const u8 *src)
{
  const u8 *p = bson_find_key(src, "input");
  if (p)
    return bson_read_int32(p, "prevkey", &old_key);
  return false;
}

unsigned input_write_savestate(u8 *dst)
{
  u8 *wbptr1, *startp = dst;
  bson_start_document(dst, "input", wbptr1);
  bson_write_int32(dst, "prevkey", old_key);
  bson_finish_document(dst, wbptr1);
  return (unsigned int)(dst - startp);
}


