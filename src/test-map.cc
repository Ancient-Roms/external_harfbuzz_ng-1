/*
 * Copyright © 2021  Behdad Esfahbod
 *
 *  This is part of HarfBuzz, a text shaping library.
 *
 * Permission is hereby granted, without written agreement and without
 * license or royalty fees, to use, copy, modify, and distribute this
 * software and its documentation for any purpose, provided that the
 * above copyright notice and the following two paragraphs appear in
 * all copies of this software.
 *
 * IN NO EVENT SHALL THE COPYRIGHT HOLDER BE LIABLE TO ANY PARTY FOR
 * DIRECT, INDIRECT, SPECIAL, INCIDENTAL, OR CONSEQUENTIAL DAMAGES
 * ARISING OUT OF THE USE OF THIS SOFTWARE AND ITS DOCUMENTATION, EVEN
 * IF THE COPYRIGHT HOLDER HAS BEEN ADVISED OF THE POSSIBILITY OF SUCH
 * DAMAGE.
 *
 * THE COPYRIGHT HOLDER SPECIFICALLY DISCLAIMS ANY WARRANTIES, INCLUDING,
 * BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND
 * FITNESS FOR A PARTICULAR PURPOSE.  THE SOFTWARE PROVIDED HEREUNDER IS
 * ON AN "AS IS" BASIS, AND THE COPYRIGHT HOLDER HAS NO OBLIGATION TO
 * PROVIDE MAINTENANCE, SUPPORT, UPDATES, ENHANCEMENTS, OR MODIFICATIONS.
 *
 */

#include "hb.hh"
#include "hb-map.hh"


int
main (int argc, char **argv)
{

  /* Test copy constructor. */
  {
    hb_map_t v1;
    v1.set (1, 2);
    hb_map_t v2 {v1};
    assert (v1.get_population () == 1);
    assert (v2.get_population () == 1);
    assert (v1[1] == 2);
    assert (v2[1] == 2);
  }

  /* Test copy assignment. */
  {
    hb_map_t v1;
    v1.set (1, 2);
    hb_map_t v2 = v1;
    assert (v1.get_population () == 1);
    assert (v2.get_population () == 1);
    assert (v1[1] == 2);
    assert (v2[1] == 2);
  }

  /* Test move constructor. */
  {
    hb_map_t v {hb_map_t {}};
  }

  /* Test move assignment. */
  {
    hb_map_t v;
    v = hb_map_t {};
  }

  return 0;
}
