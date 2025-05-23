/***************************************************************************
 *             __________               __   ___.
 *   Open      \______   \ ____   ____ |  | _\_ |__   _______  ___
 *   Source     |       _//  _ \_/ ___\|  |/ /| __ \ /  _ \  \/  /
 *   Jukebox    |    |   (  <_> )  \___|    < | \_\ (  <_> > <  <
 *   Firmware   |____|_  /\____/ \___  >__|_ \|___  /\____/__/\_ \
 *                     \/            \/     \/    \/            \/
 * $Id$
 *
 * Copyright (C) 2007 Nicolas Pennequin, Dan Everton, Matthias Mohr
 *               2010 Jonathan Gordon
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This software is distributed on an "AS IS" basis, WITHOUT WARRANTY OF ANY
 * KIND, either express or implied.
 *
 ****************************************************************************/

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "config.h"
#ifndef __PCTOOL__
#include "core_alloc.h"
#endif
#include "file.h"
#include "misc.h"
#include "plugin.h"
#include "viewport.h"

#include "skin_buffer.h"
#include "skin_debug.h"
#include "skin_parser.h"
#include "tag_table.h"

#ifdef __PCTOOL__
#ifdef WPSEDITOR
#include "proxy.h"
#include "sysfont.h"
#else
#include "action.h"
#include "checkwps.h"
#include "language.h"
#include "lang_enum.h"
#include "audio.h"
#define lang_is_rtl() (false)
#define DEBUGF printf
#define splashf(...)
#endif /*WPSEDITOR*/
#else
#include "debug.h"
#include "splash.h"
#include "lang.h"
#include "language.h"
#endif /*__PCTOOL__*/

#include <ctype.h>
#include <stdbool.h>
#include "font.h"

#include "wps_internals.h"
#include "skin_engine.h"
#include "settings.h"
#include "settings_list.h"
#include "rbpaths.h"
#include "pathfuncs.h"
#if CONFIG_TUNER
#include "radio.h"
#include "tuner.h"
#endif

#include "bmp.h"

#ifdef HAVE_ALBUMART
#include "playback.h"
#endif

#include "backdrop.h"
#include "statusbar-skinned.h"

#define WPS_ERROR_INVALID_PARAM         -1

static char* skin_buffer = NULL;
#if (LCD_DEPTH > 1) || (defined(HAVE_REMOTE_LCD) && (LCD_REMOTE_DEPTH > 1))
static char *backdrop_filename;
#endif
static struct skin_stats *_stats = NULL;

static bool isdefault(struct skin_tag_parameter *param)
{
    return param->type == DEFAULT;
}

static inline char*
get_param_text(struct skin_element *element, int param_number)
{
    struct skin_tag_parameter* params = SKINOFFSETTOPTR(skin_buffer, element->params);
    return SKINOFFSETTOPTR(skin_buffer, params[param_number].data.text);
}

static inline struct skin_element*
get_param_code(struct skin_element *element, int param_number)
{
    struct skin_tag_parameter* params = SKINOFFSETTOPTR(skin_buffer, element->params);
    return SKINOFFSETTOPTR(skin_buffer, params[param_number].data.code);
}

static inline struct skin_tag_parameter*
get_param(struct skin_element *element, int param_number)
{
    struct skin_tag_parameter* params = SKINOFFSETTOPTR(skin_buffer, element->params);
    return &params[param_number];
}

/* which screen are we parsing for? */
static enum screen_type curr_screen;

/* the current viewport */
static struct skin_element *curr_viewport_element;
static struct skin_viewport *curr_vp;
static struct skin_element *first_viewport;

static struct line *curr_line;

static int follow_lang_direction = 0;

typedef int (*parse_function)(struct skin_element *element,
                              struct wps_token *token,
                              struct wps_data *wps_data);

/* add a skin_token_list item to the list chain. ALWAYS appended because some of the
 * chains require the order to be kept.
 */
static void add_to_ll_chain(OFFSETTYPE(struct skin_token_list *) *listoffset,
        struct skin_token_list *item)
{
    struct skin_token_list *list = SKINOFFSETTOPTR(skin_buffer, *listoffset);
    if (list == NULL)
    {
        *listoffset = PTRTOSKINOFFSET(skin_buffer, item);
    }
    else
    {
        while (SKINOFFSETTOPTR(skin_buffer, list->next))
            list = SKINOFFSETTOPTR(skin_buffer, list->next);
        list->next = PTRTOSKINOFFSET(skin_buffer, item);
    }
}

void *skin_find_item(const char *label, enum skin_find_what what,
                     struct wps_data *data)
{
    char *databuf = get_skin_buffer(data);
    union {
        struct skin_token_list *linkedlist;
        struct skin_element *vplist;
    } list = {NULL};
    bool isvplist = false;
    void *ret = NULL;
    if (!databuf)
        databuf = skin_buffer;
    switch (what)
    {
        case SKIN_FIND_UIVP:
        case SKIN_FIND_VP:
            list.vplist = SKINOFFSETTOPTR(databuf, data->tree);
            isvplist = true;
        break;
        case SKIN_FIND_IMAGE:
            list.linkedlist = SKINOFFSETTOPTR(databuf, data->images);
        break;
#ifdef HAVE_TOUCHSCREEN
        case SKIN_FIND_TOUCHREGION:
            list.linkedlist = SKINOFFSETTOPTR(databuf, data->touchregions);
        break;
#endif
#ifdef HAVE_SKIN_VARIABLES
        case SKIN_VARIABLE:
            list.linkedlist = SKINOFFSETTOPTR(databuf, data->skinvars);
        break;
#endif
    }

    while (list.linkedlist)
    {
        bool skip = false;
        struct wps_token *token = NULL;
        const char *itemlabel = NULL;
        if (!isvplist)
            token = SKINOFFSETTOPTR(databuf, list.linkedlist->token);
        switch (what)
        {
            case SKIN_FIND_UIVP:
            case SKIN_FIND_VP:
                ret = SKINOFFSETTOPTR(databuf, list.vplist->data);
                if (!ret) break;
                if (((struct skin_viewport *)ret)->label == VP_DEFAULT_LABEL)
                    itemlabel = VP_DEFAULT_LABEL_STRING;
                else
                    itemlabel = SKINOFFSETTOPTR(databuf, ((struct skin_viewport *)ret)->label);
                skip = !(((struct skin_viewport *)ret)->is_infovp ==
                    (what==SKIN_FIND_UIVP));
                break;
            case SKIN_FIND_IMAGE:
                if (!token) break;
                ret = SKINOFFSETTOPTR(databuf, token->value.data);
                if (!ret) break;
                itemlabel = SKINOFFSETTOPTR(databuf, ((struct gui_img *)ret)->label);
                break;
#ifdef HAVE_TOUCHSCREEN
            case SKIN_FIND_TOUCHREGION:
                if (!token) break;
                ret = SKINOFFSETTOPTR(databuf, token->value.data);
                if (!ret) break;
                itemlabel = SKINOFFSETTOPTR(databuf, ((struct touchregion *)ret)->label);
                break;
#endif
#ifdef HAVE_SKIN_VARIABLES
            case SKIN_VARIABLE:
                if (!token) break;
                ret = SKINOFFSETTOPTR(databuf, token->value.data);
                if (!ret) break;
                itemlabel = SKINOFFSETTOPTR(databuf, ((struct skin_var *)ret)->label);
                break;
#endif
        }
        if (!skip && itemlabel && !strcmp(itemlabel, label))
        {
            return ret;
        }

        if (isvplist)
            list.vplist = SKINOFFSETTOPTR(databuf, list.vplist->next);
        else
            list.linkedlist = SKINOFFSETTOPTR(databuf, list.linkedlist->next);
    }
    return NULL;
}

/* create and init a new wpsll item.
 * passing NULL to token will alloc a new one.
 * You should only pass NULL for the token when the token type (table above)
 * is WPS_NO_TOKEN which means it is not stored automatically in the skins token array
 */
static struct skin_token_list *new_skin_token_list_item(struct wps_token *token,
                                                        void* token_data)
{
    struct skin_token_list *llitem = skin_buffer_alloc(sizeof(*llitem));
    if (!token)
        token = skin_buffer_alloc(sizeof(*token));
    if (!llitem || !token)
        return NULL;
    llitem->next = PTRTOSKINOFFSET(skin_buffer, NULL);
    llitem->token = PTRTOSKINOFFSET(skin_buffer, token);
    if (token_data)
        token->value.data = PTRTOSKINOFFSET(skin_buffer, token_data);
    return llitem;
}

static int parse_statusbar_tags(struct skin_element* element,
                                struct wps_token *token,
                                struct wps_data *wps_data)
{
    (void)element;
    if (token->type == SKIN_TOKEN_DRAW_INBUILTBAR)
    {
        token->value.data = PTRTOSKINOFFSET(skin_buffer, (void*)&curr_vp->vp);
    }
    else
    {
        struct skin_viewport *skin_default = SKINOFFSETTOPTR(skin_buffer, first_viewport->data);
        if (first_viewport->params_count == 0)
        {
            wps_data->wps_sb_tag = true;
            wps_data->show_sb_on_wps = (token->type == SKIN_TOKEN_ENABLE_THEME);
        }
        if (wps_data->show_sb_on_wps)
        {
            viewport_set_defaults(&skin_default->vp, curr_screen);
        }
        else
        {
            viewport_set_fullscreen(&skin_default->vp, curr_screen);
        }
#ifdef HAVE_REMOTE_LCD
        /* This parser requires viewports which will use the settings font to
         * have font == 1, but the above viewport_set() calls set font to
         * the current real font id. So force 1 here it will be set correctly
         * at the end
         */
        skin_default->vp.font = 1;
#endif
    }
    return 0;
}

static int get_image_id(int c)
{
    if(c >= 'a' && c <= 'z')
        return c - 'a';
    else if(c >= 'A' && c <= 'Z')
        return c - 'A' + 26;
    else
        return -1;
}

void get_image_filename(const char *start, const char* bmpdir,
                                char *buf, int buf_size)
{
    path_append(buf, bmpdir, start, buf_size);
}

static int parse_image_display(struct skin_element *element,
                               struct wps_token *token,
                               struct wps_data *wps_data)
{
    char *label = get_param_text(element, 0);
    char sublabel = '\0';
    int subimage;
    struct gui_img *img;
    struct image_display *id = skin_buffer_alloc(sizeof(*id));

    if (element->params_count == 1 && strlen(label) <= 2)
    {
        /* backwards compatability. Allow %xd(Aa) to still work */
        sublabel = label[1];
        label[1] = '\0';
    }
    /* sanity check */
    img = skin_find_item(label, SKIN_FIND_IMAGE, wps_data);
    if (!img || !id)
    {
        return WPS_ERROR_INVALID_PARAM;
    }
    id->label = img->label;
    id->offset = 0;
    id->token = PTRTOSKINOFFSET(skin_buffer, NULL);
    if (img->using_preloaded_icons)
    {
        token->type = SKIN_TOKEN_IMAGE_DISPLAY_LISTICON;
    }

    if (token->type == SKIN_TOKEN_IMAGE_DISPLAY_9SEGMENT)
        img->is_9_segment = true;

    if (element->params_count > 1)
    {
        struct skin_tag_parameter *param1 = get_param(element, 1);
        if (param1->type == CODE)
            id->token = get_param_code(element, 1)->data;
        /* specify a number. 1 being the first subimage (i.e top) NOT 0 */
        else if (param1->type == INTEGER)
            id->subimage = param1->data.number - 1;
        if (element->params_count > 2)
            id->offset = get_param(element, 2)->data.number;
    }
    else
    {
        if ((subimage = get_image_id(sublabel)) != -1)
        {
            if (subimage >= img->num_subimages)
                return WPS_ERROR_INVALID_PARAM;
            id->subimage = subimage;
        } else {
            id->subimage = 0;
        }
    }
    token->value.data = PTRTOSKINOFFSET(skin_buffer, id);
    return 0;
}

static int parse_image_load(struct skin_element *element,
                            struct wps_token *token,
                            struct wps_data *wps_data)
{
    const char* filename;
    const char* id;
    int x = 0,y = 0, subimages = 1;
    struct gui_img *img;

    /* format: %x(n,filename.bmp[,x,y])
       or %xl(n,filename.bmp[,x,y])
       or %xl(n,filename.bmp[,x,y,num_subimages])
    */

    id = get_param_text(element, 0);
    filename = get_param_text(element, 1);
    /* x,y,num_subimages handling:
     * If all 3 are left out use sane defaults.
     * If there are 2 params it must be x,y
     * if there is only 1 param it must be the num_subimages
     */
    if (element->params_count == 3)
        subimages = get_param(element, 2)->data.number;
    else if (element->params_count > 3)
    {
        struct skin_tag_parameter *param;
        param = get_param(element, 2);
        if (param->type == PERCENT)
            x = param->data.number * curr_vp->vp.width / 1000;
        else
            x = param->data.number;

        param = get_param(element, 3);
        if (param->type == PERCENT)
            y = param->data.number * curr_vp->vp.height / 1000;
        else
            y = param->data.number;

        if (element->params_count == 5)
            subimages = get_param(element, 4)->data.number;
    }
    /* check the image number and load state */
    if(skin_find_item(id, SKIN_FIND_IMAGE, wps_data))
    {
        /* Invalid image ID */
        return WPS_ERROR_INVALID_PARAM;
    }
    img = skin_buffer_alloc(sizeof(*img));
    if (!img)
        return WPS_ERROR_INVALID_PARAM;
    /* save a pointer to the filename */
    img->bm.data = (char*)filename;
    img->label = PTRTOSKINOFFSET(skin_buffer, (void*)id);
    img->x = x;
    img->y = y;
    img->num_subimages = subimages;
    img->display = -1;
    img->using_preloaded_icons = false;
    img->buflib_handle = -1;
    img->is_9_segment = false;
    img->loaded = false;
    img->dither = false;

    if (token->type == SKIN_TOKEN_IMAGE_DISPLAY)
    {
        token->value.data = PTRTOSKINOFFSET(skin_buffer, img);
#ifdef HAVE_BACKDROP_IMAGE
        if (curr_vp)
            img->dither = curr_vp->output_to_backdrop_buffer;
#endif
    }

    if (!strcmp(img->bm.data, "__list_icons__"))
    {
        img->num_subimages = Icon_Last_Themeable;
        img->using_preloaded_icons = true;
    }

    struct skin_token_list *item = new_skin_token_list_item(NULL, img);
    if (!item)
        return WPS_ERROR_INVALID_PARAM;
    add_to_ll_chain(&wps_data->images, item);

    return 0;
}
struct skin_font {
    int id; /* the id from font_load */
    char *name;  /* filename without path and extension */
    int glyphs;  /* how many glyphs to reserve room for */
};
static struct skin_font skinfonts[MAXUSERFONTS];
static int parse_font_load(struct skin_element *element,
                           struct wps_token *token,
                           struct wps_data *wps_data)
{
    (void)wps_data; (void)token;
    int id = get_param(element, 0)->data.number;
    char *filename = get_param_text(element, 1);
    int  glyphs;
    char *ptr;

    if(element->params_count > 2)
        glyphs = get_param(element, 2)->data.number;
    else
        glyphs = global_settings.glyphs_to_cache;
    if (id < 2)
    {
        DEBUGF("font id must be >= 2 (%d)\n", id);
        return -1;
    }
#if defined(DEBUG) || defined(SIMULATOR)
    if (skinfonts[id-2].name != NULL)
    {
        DEBUGF("font id %d already being used\n", id);
    }
#endif
    /* make sure the filename contains .fnt,
     * we dont actually use it, but require it anyway */
    ptr = strrchr(filename, '.');
    if (!ptr || strncmp(ptr, ".fnt", 4))
        return WPS_ERROR_INVALID_PARAM;
    skinfonts[id-2].id = -1;
    skinfonts[id-2].name = filename;
    skinfonts[id-2].glyphs = glyphs;

    return 0;
}

static int parse_playlistview(struct skin_element *element,
                              struct wps_token *token,
                              struct wps_data *wps_data)
{
    (void)wps_data;
    struct playlistviewer *viewer = skin_buffer_alloc(sizeof(*viewer));
    if (!viewer)
        return WPS_ERROR_INVALID_PARAM;
    viewer->show_icons = true;
    viewer->start_offset = get_param(element, 0)->data.number;
    viewer->line = PTRTOSKINOFFSET(skin_buffer, get_param_code(element, 1));

    token->value.data = PTRTOSKINOFFSET(skin_buffer, (void*)viewer);

    return 0;
}

#ifdef HAVE_LCD_COLOR
static int parse_viewport_gradient_setup(struct skin_element *element,
                                   struct wps_token *token,
                                   struct wps_data *wps_data)
{
    (void)wps_data;
    struct gradient_config *cfg;
    if (element->params_count < 2) /* only start and end are required */
        return 1;
    cfg = skin_buffer_alloc(sizeof(*cfg));
    if (!cfg)
        return 1;
    if (!parse_color(curr_screen, get_param_text(element, 0), &cfg->start) ||
        !parse_color(curr_screen, get_param_text(element, 1), &cfg->end))
        return 1;
    if (element->params_count > 2)
    {
        if (!parse_color(curr_screen, get_param_text(element, 2), &cfg->text))
            return 1;
    }
    else
    {
        cfg->text = curr_vp->vp.fg_pattern;
    }

    token->value.data = PTRTOSKINOFFSET(skin_buffer, cfg);
    return 0;
}
#endif

static int parse_listitem(struct skin_element *element,
                        struct wps_token *token,
                        struct wps_data *wps_data)
{
    (void)wps_data;
    struct listitem *li = skin_buffer_alloc(sizeof(*li));
    if (!li)
        return -1;
    token->value.data = PTRTOSKINOFFSET(skin_buffer, li);
    if (element->params_count == 0)
        li->offset = 0;
    else
    {
        li->offset = get_param(element, 0)->data.number;
        if (element->params_count > 1)
            li->wrap = strcasecmp(get_param_text(element, 1), "nowrap") != 0;
        else
            li->wrap = true;
    }
    return 0;
}

static int parse_listitemviewport(struct skin_element *element,
                                  struct wps_token *token,
                                  struct wps_data *wps_data)
{
#ifndef __PCTOOL__
    struct skin_tag_parameter *param;
    struct listitem_viewport_cfg *cfg = skin_buffer_alloc(sizeof(*cfg));
    if (!cfg)
        return -1;
    cfg->data = wps_data;
    cfg->tile = false;
    cfg->label = PTRTOSKINOFFSET(skin_buffer, get_param_text(element, 0));
    cfg->width = -1;
    cfg->height = -1;

    param = get_param(element, 1);
    if (!isdefault(param))
        cfg->width = param->data.number;

    param = get_param(element, 2);
    if (!isdefault(param))
        cfg->height = param->data.number;

    if (element->params_count > 3 &&
        !strcmp(get_param_text(element, 3), "tile"))
        cfg->tile = true;
    token->value.data = PTRTOSKINOFFSET(skin_buffer, (void*)cfg);
#endif
    return 0;
}

#if (LCD_DEPTH > 1) || (defined(HAVE_REMOTE_LCD) && (LCD_REMOTE_DEPTH > 1))
static int parse_viewporttextstyle(struct skin_element *element,
                                   struct wps_token *token,
                                   struct wps_data *wps_data)
{
    (void)wps_data;
    char *mode = get_param_text(element, 0);
    struct line_desc *line = skin_buffer_alloc(sizeof(*line));
    *line = (struct line_desc)LINE_DESC_DEFINIT;
    unsigned colour;

    static const char * const vp_options[] = { "invert", "color", "colour",
                                 "clear", "gradient", NULL};

    int vp_op = string_option(mode, vp_options, false);

    if (vp_op == 0) /*invert*/
    {
        line->style = STYLE_INVERT;
    }
    else if (vp_op == 1 || vp_op == 2) /*color/colour*/
    {
        if (element->params_count < 2 ||
            !parse_color(curr_screen, get_param_text(element, 1), &colour))
            return 1;
        /* STYLE_COLORED is only a modifier and can't be used on its own */
        line->style = STYLE_COLORED | STYLE_DEFAULT;
        line->text_color = colour;
    }
#ifdef HAVE_LCD_COLOR
    else if (vp_op == 4) /*gradient*/
    {
        int num_lines;
        if (element->params_count < 2)
            num_lines = 1;
        else /* atoi() instead of using a number in the parser is because [si]
              * will select the number for something which looks like a colour
              * making the "colour" case (above) harder to parse */
            num_lines = atoi(get_param_text(element, 1));
        line->style = STYLE_GRADIENT;
        line->nlines = num_lines;
    }
#endif
    else if (vp_op == 3) /*clear*/
    {
        line->style = STYLE_DEFAULT;
    }
    else
        return 1;

    token->value.data = PTRTOSKINOFFSET(skin_buffer, line);
    return 0;
}

static int parse_drawrectangle( struct skin_element *element,
                                struct wps_token *token,
                                struct wps_data *wps_data)
{
    (void)wps_data;
    struct skin_tag_parameter *param;
    struct draw_rectangle *rect = skin_buffer_alloc(sizeof(*rect));

    if (!rect)
        return -1;

    param = get_param(element, 0);
    if (param->type == PERCENT)
        rect->x = param->data.number * curr_vp->vp.width / 1000;
    else
        rect->x = param->data.number;

    param = get_param(element, 1);
    if (param->type == PERCENT)
        rect->y = param->data.number * curr_vp->vp.height / 1000;
    else
        rect->y = param->data.number;

    param = get_param(element, 2);
    if (isdefault(param))
        rect->width = curr_vp->vp.width - rect->x;
    else if (param->type == PERCENT)
        rect->width = param->data.number * curr_vp->vp.width / 1000;
    else
        rect->width = param->data.number;

    param = get_param(element, 3);
    if (isdefault(param))
        rect->height = curr_vp->vp.height - rect->y;
    else if (param->type == PERCENT)
        rect->height = param->data.number * curr_vp->vp.height / 1000;
    else
        rect->height = param->data.number;

    rect->start_colour = curr_vp->vp.fg_pattern;
    rect->end_colour = curr_vp->vp.fg_pattern;

    if (element->params_count > 4)
    {
        if (!parse_color(curr_screen, get_param_text(element, 4),
                    &rect->start_colour))
            return -1;
        rect->end_colour = rect->start_colour;
    }
    if (element->params_count > 5)
    {
        if (!parse_color(curr_screen, get_param_text(element, 5),
                    &rect->end_colour))
            return -1;
    }
    token->value.data = PTRTOSKINOFFSET(skin_buffer, rect);

    return 0;
}

static int parse_viewportcolour(struct skin_element *element,
                                struct wps_token *token,
                                struct wps_data *wps_data)
{
    (void)wps_data;
    struct skin_tag_parameter *param = get_param(element, 0);
    struct viewport_colour *colour = skin_buffer_alloc(sizeof(*colour));
    if (!colour)
        return -1;
    if (isdefault(param))
    {
        unsigned int fg_color;
        unsigned int bg_color;

        switch (curr_screen)
        {
#if defined(HAVE_REMOTE_LCD) && LCD_REMOTE_DEPTH > 1
        case SCREEN_REMOTE:
            fg_color = LCD_REMOTE_DEFAULT_FG;
            bg_color = LCD_REMOTE_DEFAULT_BG;
            break;
#endif
        default:
#if defined(HAVE_LCD_COLOR)
            fg_color = global_settings.fg_color;
            bg_color = global_settings.bg_color;
#elif LCD_DEPTH > 1
            fg_color = LCD_DEFAULT_FG;
            bg_color = LCD_DEFAULT_BG;
#else
            fg_color = 0;
            bg_color = 0;
#endif
            break;
        }

        if (token->type == SKIN_TOKEN_VIEWPORT_FGCOLOUR)
            colour->colour = fg_color;
        else
            colour->colour = bg_color;
    }
    else
    {
        if (!parse_color(curr_screen, SKINOFFSETTOPTR(skin_buffer, param->data.text),
                    &colour->colour))
            return -1;
    }
    token->value.data = PTRTOSKINOFFSET(skin_buffer, colour);
    if (element->line == curr_viewport_element->line)
    {
        if (token->type == SKIN_TOKEN_VIEWPORT_FGCOLOUR)
            curr_vp->vp.fg_pattern = colour->colour;
        else
            curr_vp->vp.bg_pattern = colour->colour;
    }
    return 0;
}

static int parse_image_special(struct skin_element *element,
                               struct wps_token *token,
                               struct wps_data *wps_data)
{
    (void)wps_data; /* kill warning */
    (void)token;

#if LCD_DEPTH > 1
    char *filename;
    if (token->type == SKIN_TOKEN_IMAGE_BACKDROP)
    {
        if (isdefault(get_param(element, 0)))
        {
            filename = "-";
        }
        else
        {
            filename = get_param_text(element, 0);
            /* format: %X(filename.bmp) or %X(d) */
            if (!strcmp(filename, "d"))
                filename = NULL;
        }
        backdrop_filename = filename;
    }
#endif

    return 0;
}
#endif

static int parse_progressbar_tag(struct skin_element* element,
                                 struct wps_token *token,
                                 struct wps_data *wps_data);

static int parse_setting_and_lang(struct skin_element *element,
                                  struct wps_token *token,
                                  struct wps_data *wps_data)
{
    (void)wps_data;
    char *temp = get_param_text(element, 0);

    if (token->type == SKIN_TOKEN_TRANSLATEDSTRING)
    {
        int i = lang_english_to_id(temp);
        if (i < 0) {
            DEBUGF("Translated String [%s] NOT FOUND\n", temp);
            /* Due to conditionals in a theme, a missing string
               might never be hit.  So currently we have to just treat
               this as an advisory */
#if 1
            i = LANG_LAST_INDEX_IN_ARRAY;
#else
            return WPS_ERROR_INVALID_PARAM;
#endif
        }
#ifdef DEBUG_SKIN_ENGINE
        else if (debug_wps) {
            DEBUGF("Translated String [%s] = %d\n", temp, i);
        }
#endif
        token->value.i = i;
    }
    else if (element->params_count > 1)
    {
        if (element->params_count > 4)
            return parse_progressbar_tag(element, token, wps_data);
        else
            return WPS_ERROR_INVALID_PARAM;
    }
    else
    {
    /* NOTE: The string validations that happen here will
     * automatically PASS on checkwps because its too hard to get
     * settings_list.c built for a specific target.
     * If that ever changes remove the #ifndef __PCTOOL__ here
     */
#ifndef __PCTOOL__
        const struct settings_list *setting = find_setting_by_cfgname(temp);
        if (!setting) {
            DEBUGF("Invalid setting [%s]\n", temp);
            return WPS_ERROR_INVALID_PARAM;
        }
        token->value.xdata = (void *)setting;
#endif
    }
    return 0;
}

static int parse_logical_andor(struct skin_element *element,
                             struct wps_token *token,
                             struct wps_data *wps_data)
{
    (void)wps_data;
    token->value.data = PTRTOSKINOFFSET(skin_buffer, element);
    return 0;
}

static int parse_logical_if(struct skin_element *element,
                             struct wps_token *token,
                             struct wps_data *wps_data)
{
    (void)wps_data;
    char *op = get_param_text(element, 1);
    struct logical_if *lif = skin_buffer_alloc(sizeof(*lif));
    if (!lif)
        return -1;
    token->value.data = PTRTOSKINOFFSET(skin_buffer, lif);
    lif->token = get_param_code(element, 0)->data;

    /* one or two operator conditionals */
    #define OPS2VAL(op1, op2) ((int)op1 << 8 | (int)op2)
    #define CLAUSE(op1, op2, symbol) {OPS2VAL(op1, op2), symbol }

    struct clause_symbol {int value;int symbol;};
    static const struct clause_symbol get_clause_match[] =
    {
        CLAUSE('=', '=', IF_EQUALS),
        CLAUSE('!', '=', IF_NOTEQUALS),
        CLAUSE('>', '=', IF_GREATERTHAN_EQ),
        CLAUSE('<', '=', IF_LESSTHAN_EQ),
        /*All Single value items @ end */
        CLAUSE('>', 0, IF_GREATERTHAN),
        CLAUSE('<', 0, IF_LESSTHAN),
        CLAUSE('=', 0, IF_EQUALS),
    };

    int val1 = OPS2VAL(op[0], 0);
    int val2;
    if (val1 != 0) /* Empty string ?*/
    {
        val2 = OPS2VAL(op[0], op[1]);
        for (unsigned int i = 0; i < ARRAYLEN(get_clause_match); i++)
        {
            const struct clause_symbol *sym = &get_clause_match[i];
            if(sym->value == val1 || sym->value == val2)
            {
                lif->op = sym->symbol;
                break;
            }
        }
    }
    memcpy(&lif->operand, get_param(element, 2), sizeof(lif->operand));
    if (element->params_count > 3)
        lif->num_options = get_param(element, 3)->data.number;
    else
        lif->num_options = TOKEN_VALUE_ONLY;
    return 0;

}

static int parse_timeout_tag(struct skin_element *element,
                             struct wps_token *token,
                             struct wps_data *wps_data)
{
    (void)wps_data;
    int val = 0;
    int params_count = element->params_count;
    if (params_count == 0)
    {
        switch (token->type)
        {
            case SKIN_TOKEN_SUBLINE_TIMEOUT:
                return -1;
            case SKIN_TOKEN_BUTTON_VOLUME:
            case SKIN_TOKEN_TRACK_STARTING:
            case SKIN_TOKEN_TRACK_ENDING:
                val = 10;
                break;
            default:
                break;
        }
    }
    else
    {
        val = get_param(element, 0)->data.number;
        if (token->type == SKIN_TOKEN_SUBLINE_TIMEOUT && params_count == 2)
        {
            struct wps_subline_timeout *st = skin_buffer_alloc(sizeof(*st));
            if (st)
            {
                st->show = val;
                st->hide = get_param(element, 1)->data.number;
#ifndef __PCTOOL__
                st->next_tick = current_tick; /* show immediately the first time */
#else
                st->next_tick = 0; /* checkwps doesn't have current_tick */
#endif
                token->type = SKIN_TOKEN_SUBLINE_TIMEOUT_HIDE;
                token->value.data = PTRTOSKINOFFSET(skin_buffer, st);
                return 0;
            }
        }
    }
    token->value.i = val * TIMEOUT_UNIT;
    return 0;
}

static int parse_substring_tag(struct skin_element* element,
                                 struct wps_token *token,
                                 struct wps_data *wps_data)
{
    (void)wps_data;
    struct substring *ss = skin_buffer_alloc(sizeof(*ss));
    if (!ss)
        return 1;
    ss->start = get_param(element, 0)->data.number;
    if (get_param(element, 1)->type == DEFAULT)
        ss->length = -1;
    else
        ss->length = get_param(element, 1)->data.number;
    ss->token = get_param_code(element, 2)->data;
    if (element->params_count > 3)
        ss->expect_number = !strcmp(get_param_text(element, 3), "number");
    else
        ss->expect_number = false;
    token->value.data = PTRTOSKINOFFSET(skin_buffer, ss);
    return 0;
}

static int parse_progressbar_tag(struct skin_element* element,
                                 struct wps_token *token,
                                 struct wps_data *wps_data)
{
    struct progressbar *pb;
    struct viewport *vp = &curr_vp->vp;
    struct skin_tag_parameter *param = get_param(element, 0);
    int curr_param = 0;
    int setting_offset = 0;
    char *image_filename = NULL;
#ifdef HAVE_TOUCHSCREEN
    bool suppress_touchregion = false;
#endif

    if (element->params_count == 0 &&
        element->tag->type != SKIN_TOKEN_PROGRESSBAR)
        return 0; /* nothing to do */
    pb = skin_buffer_alloc(sizeof(*pb));

    token->value.data = PTRTOSKINOFFSET(skin_buffer, pb);

    if (!pb)
        return WPS_ERROR_INVALID_PARAM;
    pb->follow_lang_direction = follow_lang_direction > 0;
    pb->nofill = false;
    pb->noborder = false;
    pb->nobar = false;
    pb->image = PTRTOSKINOFFSET(skin_buffer, NULL);
    pb->slider = PTRTOSKINOFFSET(skin_buffer, NULL);
    pb->backdrop = PTRTOSKINOFFSET(skin_buffer, NULL);
    pb->setting = NULL;
    pb->invert_fill_direction = false;
    pb->horizontal = true;

    if (element->params_count == 0)
    {
        pb->x = 0;
        pb->width = vp->width;
        pb->height = SYSFONT_HEIGHT-2;
        pb->y = -1; /* Will be computed during the rendering */
        pb->type = element->tag->type;
        return 0;
    }

    /* (x, y, width, height, ...) */
    if (!isdefault(param))
    {
        if (param->type == PERCENT)
        {
            pb->x = param->data.number *  vp->width / 1000;
        }
        else
            pb->x = param->data.number;
        if (pb->x < 0 || pb->x >= vp->width)
            return WPS_ERROR_INVALID_PARAM;
    }
    else
        pb->x = 0;
    param++;

    if (!isdefault(param))
    {
        if (param->type == PERCENT)
        {
           pb->y = param->data.number *  vp->height / 1000;
        }
        else
            pb->y = param->data.number;
        if (pb->y < 0 || pb->y >= vp->height)
            return WPS_ERROR_INVALID_PARAM;
    }
    else
        pb->y = -1; /* computed at rendering */
    param++;

    if (!isdefault(param))
    {
        if (param->type == PERCENT)
        {
           pb->width = param->data.number *  vp->width / 1000;
        }
        else
            pb->width = param->data.number;
        if (pb->width <= 0 || (pb->x + pb->width) > vp->width)
            return WPS_ERROR_INVALID_PARAM;
    }
    else
        pb->width = vp->width - pb->x;
    param++;

    if (!isdefault(param))
    {
        int max;
        if (param->type == PERCENT)
        {
           pb->height = param->data.number *  vp->height / 1000;
        }
        else
           pb->height = param->data.number;
        /* include y in check only if it was non-default */
        max = (pb->y > 0) ? pb->y + pb->height : pb->height;
        if (pb->height <= 0 || max > vp->height)
            return WPS_ERROR_INVALID_PARAM;
    }
    else
    {
        if (vp->font > FONT_UI)
            pb->height = -1; /* calculate at display time */
        else
        {
#ifndef __PCTOOL__
            pb->height = font_get(vp->font)->height;
#else
            pb->height = 8;
#endif
        }
    }
    /* optional params, first is the image filename if it isnt recognised as a keyword */

    curr_param = 4;
    if (isdefault(get_param(element, curr_param)))
    {
        param++;
        curr_param++;
    }

    pb->horizontal = pb->width > pb->height;

    enum
    {
        eINVERT = 0, eNOFILL, eNOBORDER, eNOBAR, eSLIDER, eIMAGE,
        eBACKDROP, eVERTICAL, eHORIZONTAL, eNOTOUCH, eSETTING, eSETTING_OFFSET,
        e_PB_TAG_COUNT
    };

    static const char *pb_options[e_PB_TAG_COUNT + 1] = {[eINVERT] = "invert",
                 [eNOFILL] = "nofill", [eNOBORDER] = "noborder", [eNOBAR] = "nobar",
                 [eSLIDER] = "slider", [eIMAGE] = "image", [eBACKDROP] = "backdrop",
                 [eVERTICAL] = "vertical", [eHORIZONTAL] = "horizontal",
                 [eNOTOUCH] = "notouch", [eSETTING] = "setting", [eSETTING_OFFSET] = "soffset", [e_PB_TAG_COUNT] = NULL};
    int pb_op;

    while (curr_param < element->params_count)
    {
        char* text;
        param++;
        text = SKINOFFSETTOPTR(skin_buffer, param->data.text);

        pb_op = string_option(text, pb_options, false);
        if (pb_op == eINVERT)
            pb->invert_fill_direction = true;
        else if (pb_op == eNOFILL)
            pb->nofill = true;
        else if (pb_op == eNOBORDER)
            pb->noborder = true;
        else if (pb_op == eNOBAR)
            pb->nobar = true;
        else if (pb_op == eSLIDER)
        {
            if (curr_param+1 < element->params_count)
            {
                curr_param++;
                param++;
                text = SKINOFFSETTOPTR(skin_buffer, param->data.text);
                pb->slider = PTRTOSKINOFFSET(skin_buffer,
                        skin_find_item(text, SKIN_FIND_IMAGE, wps_data));
            }
            else /* option needs the next param */
                return -1;
        }
        else if (pb_op == eIMAGE)
        {
            if (curr_param+1 < element->params_count)
            {
                curr_param++;
                param++;
                image_filename = SKINOFFSETTOPTR(skin_buffer, param->data.text);
            }
            else /* option needs the next param */
                return -1;
        }
        else if (pb_op == eBACKDROP)
        {
            if (curr_param+1 < element->params_count)
            {
                curr_param++;
                param++;
                text = SKINOFFSETTOPTR(skin_buffer, param->data.text);
                pb->backdrop = PTRTOSKINOFFSET(skin_buffer,
                        skin_find_item(text, SKIN_FIND_IMAGE, wps_data));

            }
            else /* option needs the next param */
                return -1;
        }
        else if (pb_op == eVERTICAL)
        {
            pb->horizontal = false;
            if (isdefault(get_param(element, 3)))
                pb->height = vp->height - pb->y;
        }
        else if (pb_op == eHORIZONTAL)
            pb->horizontal = true;
#ifdef HAVE_TOUCHSCREEN
        else if (pb_op == eNOTOUCH)
            suppress_touchregion = true;
#endif
        else if (token->type == SKIN_TOKEN_SETTING && pb_op == eSETTING_OFFSET)
        {
            if (curr_param+1 < element->params_count)
            {
                curr_param++;
                param++;
                setting_offset = param->data.number;
            }
        }
        else if (token->type == SKIN_TOKEN_SETTING && pb_op == eSETTING)
        {
            if (curr_param+1 < element->params_count)
            {
                curr_param++;
                param++;
                text = SKINOFFSETTOPTR(skin_buffer, param->data.text);
#ifndef __PCTOOL__
                pb->setting = find_setting_by_cfgname(text);
                if (!pb->setting)
                    return WPS_ERROR_INVALID_PARAM;
                pb->setting_offset = setting_offset;
#endif
            }
        }
        else if (curr_param == 4)
            image_filename = text;

        curr_param++;
    }

    if (image_filename)
    {
        /* noborder is incompatible together with image. There is no border
         * anyway. */
        if (pb->noborder)
            return WPS_ERROR_INVALID_PARAM;

        pb->image = PTRTOSKINOFFSET(skin_buffer,
                skin_find_item(image_filename, SKIN_FIND_IMAGE, wps_data));
        if (!SKINOFFSETTOPTR(skin_buffer, pb->image)) /* load later */
        {
            struct gui_img *img = skin_buffer_alloc(sizeof(*img));
            if (!img)
                return WPS_ERROR_INVALID_PARAM;
            /* save a pointer to the filename */
            img->bm.data = (char*)image_filename;
            img->label = PTRTOSKINOFFSET(skin_buffer, image_filename);
            img->x = 0;
            img->y = 0;
            img->num_subimages = 1;
            img->display = -1;
            img->using_preloaded_icons = false;
            img->buflib_handle = -1;
            img->loaded = false;
            struct skin_token_list *item = new_skin_token_list_item(NULL, img);
            if (!item)
                return WPS_ERROR_INVALID_PARAM;
            add_to_ll_chain(&wps_data->images, item);
            pb->image = PTRTOSKINOFFSET(skin_buffer, img);
        }
    }

    if (token->type == SKIN_TOKEN_VOLUME)
        token->type = SKIN_TOKEN_VOLUMEBAR;
    else if (token->type == SKIN_TOKEN_BATTERY_PERCENT)
        token->type = SKIN_TOKEN_BATTERY_PERCENTBAR;
    else if (token->type == SKIN_TOKEN_TUNER_RSSI)
        token->type = SKIN_TOKEN_TUNER_RSSI_BAR;
    else if (token->type == SKIN_TOKEN_PEAKMETER_LEFT)
        token->type = SKIN_TOKEN_PEAKMETER_LEFTBAR;
    else if (token->type == SKIN_TOKEN_PEAKMETER_RIGHT)
        token->type = SKIN_TOKEN_PEAKMETER_RIGHTBAR;
    else if (token->type == SKIN_TOKEN_LIST_NEEDS_SCROLLBAR)
        token->type = SKIN_TOKEN_LIST_SCROLLBAR;
    else if (token->type == SKIN_TOKEN_SETTING)
        token->type = SKIN_TOKEN_SETTINGBAR;

    pb->type = token->type;

#ifdef HAVE_TOUCHSCREEN
    if (!suppress_touchregion &&
        (token->type == SKIN_TOKEN_VOLUMEBAR ||
         token->type == SKIN_TOKEN_PROGRESSBAR ||
         token->type == SKIN_TOKEN_SETTINGBAR))
    {
        struct touchregion *region = skin_buffer_alloc(sizeof(*region));
        struct skin_token_list *item;

        if (!region)
            return 0;

        if (token->type == SKIN_TOKEN_VOLUMEBAR)
            region->action = ACTION_TOUCH_VOLUME;
        else if (token->type == SKIN_TOKEN_SETTINGBAR)
            region->action = ACTION_TOUCH_SETTING;
        else
            region->action = ACTION_TOUCH_SCROLLBAR;

        /* try to add some extra space on either end to make pressing the
         * full bar easier. ~5% on either side
         */
        region->wpad = pb->width * 5 / 100;
        if (region->wpad > 10)
            region->wpad = 10;
        region->hpad = pb->height * 5 / 100;
        if (region->hpad > 10)
            region->hpad = 10;

        region->x = pb->x;
        if (region->x < 0)
            region->x = 0;
        region->width = pb->width;
        if (region->x + region->width > curr_vp->vp.x + curr_vp->vp.width)
            region->width = curr_vp->vp.x + curr_vp->vp.width - region->x;

        region->y = pb->y;
        if (region->y < 0)
            region->y = 0;
        region->height = pb->height;
        if (region->y + region->height > curr_vp->vp.y + curr_vp->vp.height)
            region->height = curr_vp->vp.y + curr_vp->vp.height - region->y;

        region->wvp = PTRTOSKINOFFSET(skin_buffer, curr_vp);
        region->reverse_bar = false;
        region->allow_while_locked = false;
        region->press_length = PRESS;
        region->last_press = -1;
        region->armed = false;
        region->bar = PTRTOSKINOFFSET(skin_buffer, pb);
        region->label = PTRTOSKINOFFSET(skin_buffer, NULL);

        item = new_skin_token_list_item(NULL, region);
        if (!item)
            return WPS_ERROR_INVALID_PARAM;
        add_to_ll_chain(&wps_data->touchregions, item);
    }
#endif
    return 0;
}

static int parse_filetext(struct skin_element *element,
                            struct wps_token *token,
                            struct wps_data *wps_data)
{
    (void)wps_data;
    const char* filename;
    char buf[MAX_PATH];
    char *buf_start = buf;
    int fd;
    int line = 0;
    const char *search_text = NULL;
    int search_len = 0;

    /* format: %ft(filename[,line|search text]) */
    filename = get_param_text(element, 0);

    if (element->params_count == 2)
    {
        struct skin_tag_parameter *param1 = get_param(element, 1);
        if (param1->type == INTEGER)
            line = param1->data.number;
        else if (param1->type == STRING)
        {
            search_text = get_param_text(element, 1);
            line = 0x3FF; /* 1k lines is a pretty large file */
            search_len = strlen(search_text);
            DEBUGF("%s: found search text %s\n", __func__, search_text);
        }
        else
            return WPS_ERROR_INVALID_PARAM;
    }
    else if (element->params_count != 1)
    {
        DEBUGF("%s(file, line): %s Error: param count %d\n",
                  __func__, filename, element->params_count);
        return WPS_ERROR_INVALID_PARAM;
    }

    while(*filename == PATH_SEPCH) /* no absolute paths! */
        filename++;

    path_append(buf, ROCKBOX_DIR, filename, sizeof(buf));
    DEBUGF("%s %s[%d]\n", __func__, buf, line);

    if (line > 0x3FF || (fd = open_utf8(buf, O_RDONLY)) < 0)
    {
        DEBUGF("%s: Error Opening %s\n", __func__, buf);
        goto failure;
    }

    int rd = 0;
    while (line >= 0)
    {
        if ((rd = read_line(fd, buf, sizeof(buf))) <= 0)
            break;
        if (search_text && strncasecmp(buf, search_text, search_len) == 0)
        {
            buf_start += search_len;
            rd -= search_len;
            DEBUGF("%s: found '%s' Reading '%s'\n", __func__, search_text, buf);
            break;
        }
        line--;
    }

    if (rd <= 0) /* empty line? */
    {
        DEBUGF("%s: Error(%d) Reading %s\n", __func__, rd, filename);
        goto failure;
    }

    if (element->is_conditional)
    {
        buf_start = buf;
        rd = 1; /* just alloc enough for the conditional to work*/
    }

    buf_start[rd] = '\0';

    char * skinbuf = skin_buffer_alloc(rd+1);

    if (!skinbuf)
    {
        DEBUGF("%s: Error No Buffer %s\n", __func__, filename);
        close(fd);
        return WPS_ERROR_INVALID_PARAM;
    }
    strcpy(skinbuf, buf_start);
    close(fd);
    token->value.data = PTRTOSKINOFFSET(skin_buffer, skinbuf);
    token->type = SKIN_TOKEN_STRING;
    return 0;
failure:
    element->type = COMMENT;
    element->data = INVALID_OFFSET;
    token->type = SKIN_TOKEN_NO_TOKEN;
    token->value.data = INVALID_OFFSET;
    return 0;
}

#ifdef HAVE_ALBUMART
static int parse_albumart_load(struct skin_element* element,
                               struct wps_token *token,
                               struct wps_data *wps_data)
{
    struct dim dimensions;
    int albumart_slot;
    bool swap_for_rtl = lang_is_rtl() && follow_lang_direction;
    struct skin_albumart *aa = skin_buffer_alloc(sizeof(*aa));
    (void)token; /* silence warning */
    if (!aa)
        return -1;

    /* reset albumart info in wps */
    aa->xalign = WPS_ALBUMART_ALIGN_CENTER; /* default */
    aa->yalign = WPS_ALBUMART_ALIGN_CENTER; /* default */

    struct skin_tag_parameter *param;

    param = get_param(element, 0);
    if (!isdefault(param) && param->type == PERCENT)
        aa->x = param->data.number * curr_vp->vp.width / 1000;
    else
        aa->x = param->data.number;

    param = get_param(element, 1);
    if (!isdefault(param) && param->type == PERCENT)
        aa->y = param->data.number * curr_vp->vp.height / 1000;
    else
        aa->y = param->data.number;

    param = get_param(element, 2);
    if (!isdefault(param) && param->type == PERCENT)
        aa->width = param->data.number * curr_vp->vp.width / 1000;
    else
        aa->width = param->data.number;

    param = get_param(element, 3);

    if (!isdefault(param) && param->type == PERCENT)
        aa->height = param->data.number * curr_vp->vp.height / 1000;
    else
        aa->height = param->data.number;

    aa->draw_handle = -1;

    /* if we got here, we parsed everything ok .. ! */
    if (aa->width < 0)
        aa->width = 0;

    if (aa->height < 0)
        aa->height = 0;

    if (swap_for_rtl)
        aa->x = (curr_vp->vp.width - aa->width - aa->x);

    aa->state = WPS_ALBUMART_LOAD;
    wps_data->albumart = PTRTOSKINOFFSET(skin_buffer, aa);

    dimensions.width = aa->width;
    dimensions.height = aa->height;

    albumart_slot = playback_claim_aa_slot(&dimensions);

    if (0 <= albumart_slot)
        wps_data->playback_aa_slot = albumart_slot;

    if (element->params_count > 4 && !isdefault(get_param(element, 4)))
    {
        switch (tolower(*get_param_text(element, 4)))
        {
            case 'l':
                if (swap_for_rtl)
                    aa->xalign = WPS_ALBUMART_ALIGN_RIGHT;
                else
                    aa->xalign = WPS_ALBUMART_ALIGN_LEFT;
                break;
            case 'c':
                aa->xalign = WPS_ALBUMART_ALIGN_CENTER;
                break;
            case 'r':
                if (swap_for_rtl)
                    aa->xalign = WPS_ALBUMART_ALIGN_LEFT;
                else
                    aa->xalign = WPS_ALBUMART_ALIGN_RIGHT;
                break;
        }
    }
    if (element->params_count > 5 && !isdefault(get_param(element, 5)))
    {
        switch (tolower(*get_param_text(element, 5)))
        {
            case 't':
                aa->yalign = WPS_ALBUMART_ALIGN_TOP;
                break;
            case 'c':
                aa->yalign = WPS_ALBUMART_ALIGN_CENTER;
                break;
            case 'b':
                aa->yalign = WPS_ALBUMART_ALIGN_BOTTOM;
                break;
        }
    }
    return 0;
}

#endif /* HAVE_ALBUMART */
#ifdef HAVE_SKIN_VARIABLES
static struct skin_var* find_or_add_var(const char* label,
                                        struct wps_data *data)
{
    struct skin_var* ret = skin_find_item(label, SKIN_VARIABLE, data);
    if (ret)
        return ret;

    ret = skin_buffer_alloc(sizeof(*ret));
    if (!ret)
        return ret;
    ret->label = PTRTOSKINOFFSET(skin_buffer, label);
    ret->value = 1;
    ret->last_changed = 0xffff;
    struct skin_token_list *item = new_skin_token_list_item(NULL, ret);
    if (!item)
        return NULL;
    add_to_ll_chain(&data->skinvars, item);
    return ret;
}
static int parse_skinvar(  struct skin_element *element,
                           struct wps_token *token,
                           struct wps_data *wps_data)
{
    const char* label = get_param_text(element, 0);
    struct skin_var* var = find_or_add_var(label, wps_data);
    if (!var)
        return WPS_ERROR_INVALID_PARAM;
    switch (token->type)
    {
        case SKIN_TOKEN_VAR_GETVAL:
            token->value.data = PTRTOSKINOFFSET(skin_buffer, var);
            return 0;
        case SKIN_TOKEN_VAR_SET:
        {
            static const char * const sv_options[] = {"touch", "set", "inc", "dec", NULL};

            struct skin_var_changer *data = skin_buffer_alloc(sizeof(*data));
            if (!data)
                return WPS_ERROR_INVALID_PARAM;
            data->var = PTRTOSKINOFFSET(skin_buffer, var);
            char *text_param1 = get_param_text(element, 1);
            int sv_op = string_option(text_param1, sv_options, false);

            if (!isdefault(get_param(element, 2)))
                data->newval = get_param(element, 2)->data.number;
            else if (sv_op != 0) /*!touch*/
                return WPS_ERROR_INVALID_PARAM;
            data->max = 0;
            if (sv_op == 1) /*set*/
                data->direct = true;
            else if (sv_op == 2) /*inc*/
            {
                data->direct = false;
            }
            else if (sv_op == 3) /*dec*/
            {
                data->direct = false;
                data->newval *= -1;
            }
            else if (sv_op == 0) /*touch*/
            {
                data->direct = false;
                data->newval = 0;
            }
            if (element->params_count > 3)
                data->max = get_param(element, 3)->data.number;
            token->value.data = PTRTOSKINOFFSET(skin_buffer, data);
        }
        return 0;
        case SKIN_TOKEN_VAR_TIMEOUT:
        {
            struct skin_var_lastchange *data = skin_buffer_alloc(sizeof(*data));
            if (!data)
                return WPS_ERROR_INVALID_PARAM;
            data->var = PTRTOSKINOFFSET(skin_buffer, var);
            data->timeout = 10;
            if (element->params_count > 1)
                data->timeout = get_param(element, 1)->data.number;
            data->timeout *= TIMEOUT_UNIT;
            token->value.data = PTRTOSKINOFFSET(skin_buffer, data);
        }
        default:
            return 0;
    }
}
#endif /* HAVE_SKIN_VARIABLES */
#ifdef HAVE_TOUCHSCREEN
static int parse_lasttouch(struct skin_element *element,
                           struct wps_token *token,
                           struct wps_data *wps_data)
{
    struct touchregion_lastpress *data = skin_buffer_alloc(sizeof(*data));
    int i;
    struct touchregion *region = NULL;
    if (!data)
        return WPS_ERROR_INVALID_PARAM;

    data->timeout = 10;

    for (i=0; i<element->params_count; i++)
    {
        struct skin_tag_parameter *param = get_param(element, i);
        if (param->type == STRING)
            region = skin_find_item(get_param_text(element, i),
                                          SKIN_FIND_TOUCHREGION, wps_data);
        else if (param->type == INTEGER || param->type == DECIMAL)
            data->timeout = param->data.number;
    }

    data->region = PTRTOSKINOFFSET(skin_buffer, region);
    data->timeout *= TIMEOUT_UNIT;
    token->value.data = PTRTOSKINOFFSET(skin_buffer, data);
    return 0;
}

struct touchaction {const char* s; int action;};
static const struct touchaction touchactions[] = {
    /* generic actions, convert to screen actions on use */
    {"none", ACTION_TOUCHSCREEN_IGNORE},{"lock", ACTION_TOUCH_SOFTLOCK },
    {"prev", ACTION_STD_PREV },         {"next", ACTION_STD_NEXT },
    {"hotkey", ACTION_STD_HOTKEY},      {"select", ACTION_STD_OK },
    {"menu", ACTION_STD_MENU },         {"cancel", ACTION_STD_CANCEL },
    {"contextmenu", ACTION_STD_CONTEXT},{"quickscreen", ACTION_STD_QUICKSCREEN },

    /* list/tree actions */
    { "resumeplayback", ACTION_TREE_WPS}, /* returns to previous music, WPS/FM */
    /* not really WPS specific, but no equivilant ACTION_STD_* */
    {"voldown", ACTION_WPS_VOLDOWN},    {"volup", ACTION_WPS_VOLUP},
    {"mute", ACTION_TOUCH_MUTE },

    /* generic settings changers */
    {"setting_inc", ACTION_SETTINGS_INC}, {"setting_dec", ACTION_SETTINGS_DEC},
    {"setting_set", ACTION_SETTINGS_SET},

    /* WPS specific actions */
    {"rwd", ACTION_WPS_SEEKBACK },      {"ffwd", ACTION_WPS_SEEKFWD },
    {"wps_prev", ACTION_WPS_SKIPPREV }, {"wps_next", ACTION_WPS_SKIPNEXT },
    {"browse", ACTION_WPS_BROWSE },
    {"play", ACTION_WPS_PLAY },         {"stop", ACTION_WPS_STOP },
    {"shuffle", ACTION_TOUCH_SHUFFLE }, {"repmode", ACTION_TOUCH_REPMODE },
    {"pitch", ACTION_WPS_PITCHSCREEN},  {"trackinfo", ACTION_WPS_ID3SCREEN },
    {"playlist", ACTION_WPS_VIEW_PLAYLIST },
    {"listbookmarks", ACTION_WPS_LIST_BOOKMARKS },
    {"createbookmark", ACTION_WPS_CREATE_BOOKMARK },

#if CONFIG_TUNER
    /* FM screen actions */
    /* Also allow browse, play, stop from WPS codes */
    {"mode", ACTION_FM_MODE },          {"record", ACTION_FM_RECORD },
    {"presets", ACTION_FM_PRESET},
#endif
};

static int touchregion_setup_setting(struct skin_element *element, int param_no,
                                     struct touchregion *region)
{
#ifndef __PCTOOL__
    int p = param_no;
    char *name = get_param_text(element, p++);
    const struct settings_list *setting = find_setting_by_cfgname(name);
    if (!setting)
        return WPS_ERROR_INVALID_PARAM;

    region->setting_data.setting = setting;

    if (region->action == ACTION_SETTINGS_SET)
    {
        char* text;
        int temp;
        struct touchsetting *touchsetting =
            &region->setting_data;
        if (element->params_count < p+1)
            return -1;

        text = get_param_text(element, p++);
        switch (setting->flags & F_T_MASK)
        {
        case F_T_CUSTOM:
            touchsetting->value.text = PTRTOSKINOFFSET(skin_buffer, text);
            break;
        case F_T_INT:
        case F_T_UINT:
            if (setting_get_cfgvals(setting) == NULL)
            {
                touchsetting->value.number = atoi(text);
            }
            else if (cfg_string_to_int(setting, &temp, text))
            {
                if (setting->flags & F_TABLE_SETTING)
                    touchsetting->value.number =
                        setting->table_setting->values[temp];
                else
                    touchsetting->value.number = temp;
            }
            else
                return -1;
            break;
        case F_T_BOOL:
            if (cfg_string_to_int(setting, &temp, text))
            {
                touchsetting->value.number = temp;
            }
            else
                return -1;
            break;
        default:
            return -1;
        }
    }
    return p-param_no;
#endif /* __PCTOOL__ */
    return 0;
}

static int parse_touchregion(struct skin_element *element,
                             struct wps_token *token,
                             struct wps_data *wps_data)
{
    (void)token;
    unsigned i, imax;
    int p;
    struct touchregion *region = NULL;
    const char *action;
    const char pb_string[] = "progressbar";
    const char vol_string[] = "volume";

    /* format: %T([label,], x,y,width,height,action[, ...])
     * if action starts with & the area must be held to happen
     */


    region = skin_buffer_alloc(sizeof(*region));
    if (!region)
        return WPS_ERROR_INVALID_PARAM;

    /* should probably do some bounds checking here with the viewport... but later */
    region->action = ACTION_NONE;

    /* padding is only for bars, user defined regions have no need of it */
    region->wpad = 0;
    region->hpad = 0;

    if (get_param(element, 0)->type == STRING)
    {
        region->label = PTRTOSKINOFFSET(skin_buffer, get_param_text(element, 0));
        p = 1;
        if (element->params_count < 6)
            return WPS_ERROR_INVALID_PARAM;
    }
    else
    {
        region->label = PTRTOSKINOFFSET(skin_buffer, NULL);
        p = 0;
    }
/*x*/
    struct skin_tag_parameter *param = get_param(element, p);
    region->x = 0;
    if (!isdefault(param))
    {
        if (param->type == INTEGER)
            region->x = param->data.number;
        else if (param->type == PERCENT)
            region->x = param->data.number * curr_vp->vp.width / 1000;
    }
/*y*/
    param = get_param(element, ++p);
    region->y = 0;
    if (!isdefault(param))
    {
        if (param->type == INTEGER)
            region->y = param->data.number;
        else if (param->type == PERCENT)
            region->y  =param->data.number * curr_vp->vp.width / 1000;
    }
/*width*/
    param = get_param(element, ++p);
    region->width = curr_vp->vp.width;
    if (!isdefault(param))
    {
        if (param->type == INTEGER)
            region->width =param->data.number;
        else if (param->type == PERCENT)
            region->width = curr_vp->vp.width  * param->data.number / 1000;
    }
/*height*/
    param = get_param(element, ++p);
    region->height = curr_vp->vp.height;
    if (!isdefault(param))
    {
        if (param->type == INTEGER)
            region->height =param->data.number;
        else if (param->type == PERCENT)
            region->height = curr_vp->vp.height  * param->data.number / 1000;
    }
    p++;

    region->wvp = PTRTOSKINOFFSET(skin_buffer, curr_vp);
    region->armed = false;
    region->reverse_bar = false;
    region->value = 0;
    region->last_press = -1;
    region->press_length = PRESS;
    region->allow_while_locked = false;
    region->bar = PTRTOSKINOFFSET(skin_buffer, NULL);
    action = get_param_text(element, p++);

    /* figure out the action */
    if(!strcmp(pb_string, action))
        region->action = ACTION_TOUCH_SCROLLBAR;
    else if(!strcmp(vol_string, action))
        region->action = ACTION_TOUCH_VOLUME;
    else
    {
        imax = ARRAYLEN(touchactions);
        for (i = 0; i < imax; i++)
        {
            /* try to match with one of our touchregion screens */
            if (!strcmp(touchactions[i].s, action))
            {
                region->action = touchactions[i].action;
                if (region->action == ACTION_SETTINGS_INC ||
                    region->action == ACTION_SETTINGS_DEC ||
                    region->action == ACTION_SETTINGS_SET)
                {
                    int val;
                    if (element->params_count < p+1)
                        return WPS_ERROR_INVALID_PARAM;
                    val = touchregion_setup_setting(element, p, region);
                    if (val < 0)
                        return WPS_ERROR_INVALID_PARAM;
                    p += val;
                }
                break;
            }
        }
        if (region->action == ACTION_NONE)
            return WPS_ERROR_INVALID_PARAM;
    }
    static const char * const pm_options[] = {"allow_while_locked", "reverse_bar",
                                "repeat_press", "long_press", NULL};
    int pm_op;

    while (p < element->params_count)
    {
        char* param = get_param_text(element, p++);
        pm_op = string_option(param, pm_options, false);
        if (pm_op == 0)
            region->allow_while_locked = true;
        else if (pm_op == 1)
            region->reverse_bar = true;
        else if (pm_op == 2)
            region->press_length = REPEAT;
        else if (pm_op == 3)
            region->press_length = LONG_PRESS;
    }
    struct skin_token_list *item = new_skin_token_list_item(NULL, region);
    if (!item)
        return WPS_ERROR_INVALID_PARAM;
    add_to_ll_chain(&wps_data->touchregions, item);

    if (region->action == ACTION_TOUCH_MUTE)
    {
        region->value = global_settings.volume;
    }


    return 0;
}
#endif

static bool check_feature_tag(const int type)
{
    switch (type)
    {
        case SKIN_TOKEN_RTC_PRESENT:
#if CONFIG_RTC
            return true;
#else
            return false;
#endif
        case SKIN_TOKEN_HAVE_RECORDING:
#ifdef HAVE_RECORDING
            return true;
#else
            return false;
#endif
        case SKIN_TOKEN_HAVE_TUNER:
#if CONFIG_TUNER
            if (radio_hardware_present())
                return true;
#endif
            return false;
        case SKIN_TOKEN_HAVE_TOUCH:
#ifdef HAVE_TOUCHSCREEN
            return true;
#else
            return false;
#endif

#if CONFIG_TUNER
        case SKIN_TOKEN_HAVE_RDS:
#ifdef HAVE_RDS_CAP
            return true;
#else
            return false;
#endif /* HAVE_RDS_CAP */
#endif /* CONFIG_TUNER */
        default: /* not a tag we care about, just don't skip */
            return true;
    }
}

/* This is used to free any buflib allocations before the rest of
 * wps_data is reset.
 * The call to this in settings_apply_skins() is the last chance to do
 * any core_free()'s before wps_data is trashed and those handles lost
 */
void skin_data_free_buflib_allocs(struct wps_data *wps_data)
{
    if (wps_data->wps_loaded)
        skin_buffer = get_skin_buffer(wps_data);

#ifndef __PCTOOL__
    if (!skin_buffer)
        goto abort;

    struct skin_token_list *list = SKINOFFSETTOPTR(skin_buffer, wps_data->images);
    int16_t *font_ids = SKINOFFSETTOPTR(skin_buffer, wps_data->font_ids);
    while (list)
    {
        struct wps_token *token = SKINOFFSETTOPTR(skin_buffer, list->token);
        struct gui_img *img = NULL;
        if (token)
            img = (struct gui_img*)SKINOFFSETTOPTR(skin_buffer, token->value.data);
        if (img && img->buflib_handle > 0)
        {
            struct skin_token_list *imglist = SKINOFFSETTOPTR(skin_buffer, list->next);

            core_free(img->buflib_handle);
            while (imglist)
            {
                struct wps_token *freetoken = SKINOFFSETTOPTR(skin_buffer, imglist->token);
                struct gui_img *freeimg = NULL;
                if (freetoken)
                    freeimg = (struct gui_img*)SKINOFFSETTOPTR(skin_buffer, freetoken->value.data);
                if (freeimg && img->buflib_handle == freeimg->buflib_handle)
                    freeimg->buflib_handle = -1;
                imglist = SKINOFFSETTOPTR(skin_buffer, imglist->next);
            }
        }
        list = SKINOFFSETTOPTR(skin_buffer, list->next);
    }
    if (font_ids != NULL)
    {
        while (wps_data->font_count > 0)
            font_unload(font_ids[--wps_data->font_count]);
    }

abort:
    wps_data->font_ids = PTRTOSKINOFFSET(skin_buffer, NULL); /* Safe if skin_buffer is NULL */
    wps_data->images = PTRTOSKINOFFSET(skin_buffer, NULL);
    wps_data->buflib_handle = core_free(wps_data->buflib_handle);
#endif
}

/*
 * initial setup of wps_data; does reset everything
 * except fields which need to survive, i.e.
 * Also called if the load fails
 **/
static void skin_data_reset(struct wps_data *wps_data)
{
    skin_data_free_buflib_allocs(wps_data);
    wps_data->images = INVALID_OFFSET;
    wps_data->tree = INVALID_OFFSET;
#ifdef HAVE_BACKDROP_IMAGE
    if (wps_data->backdrop_id >= 0)
        skin_backdrop_unload(wps_data->backdrop_id);
    backdrop_filename = NULL;
#endif
#ifdef HAVE_TOUCHSCREEN
    wps_data->touchregions = INVALID_OFFSET;
#endif
#ifdef HAVE_SKIN_VARIABLES
    wps_data->skinvars = INVALID_OFFSET;
#endif
#ifdef HAVE_ALBUMART
    wps_data->albumart = INVALID_OFFSET;
    if (wps_data->playback_aa_slot >= 0)
    {
        playback_release_aa_slot(wps_data->playback_aa_slot);
        wps_data->playback_aa_slot = -1;
    }
#endif

    wps_data->peak_meter_enabled = false;
    wps_data->wps_sb_tag = false;
    wps_data->show_sb_on_wps = false;
    wps_data->wps_loaded = false;
}

#ifndef __PCTOOL__
static int buflib_move_callback(int handle, void* current, void* new)
{
    (void)handle;
    (void)current;
    (void)new;
    /* Any active skins may be scrolling - which means using viewports which
     * will be moved after this callback returns. This is a hammer to make that
     * safe. TODO: use a screwdriver instead.
     */
    FOR_NB_SCREENS(i)
        screens[i].scroll_stop();

    for (int i = 0; i < SKINNABLE_SCREENS_COUNT; i++)
        skin_request_full_update(i);

    return BUFLIB_CB_OK;
}
#endif

static int load_skin_bmp(struct wps_data *wps_data, struct gui_img *img, char* bmpdir)
{

    (void)wps_data; /* only needed for remote targets */
    char img_path[MAX_PATH];
    struct bitmap *bitmap = &img->bm;

    get_image_filename(bitmap->data, bmpdir,
                       img_path, sizeof(img_path));

#ifdef __PCTOOL__ /* just check if image exists */
    int fd = open(img_path, O_RDONLY);
    if (fd < 0)
    {
        DEBUGF("Couldn't open %s\n", img_path);
        return fd;
    }
    close(fd);
    return 1;
#else /* load the image */
    static struct buflib_callbacks buflib_ops = {buflib_move_callback, NULL, NULL};
    int handle;
    int bmpformat;
    ssize_t buf_reqd;
#ifdef HAVE_REMOTE_LCD
    if (curr_screen == SCREEN_REMOTE)
        bmpformat = FORMAT_ANY|FORMAT_REMOTE;
    else
#endif
        bmpformat = img->dither ? FORMAT_ANY|FORMAT_DITHER|FORMAT_TRANSPARENT :
                                  FORMAT_ANY|FORMAT_TRANSPARENT;

    handle = core_load_bmp(img_path, bitmap, bmpformat, &buf_reqd, &buflib_ops);
    if (handle != CLB_ALOC_ERR)
    {
        /* NOTE!: bitmap->data == NULL to force a crash later if the
           caller doesnt call core_get_data() */
        _stats->buflib_handles++;
        _stats->images_size += buf_reqd;
        return handle;
    }

    if (buf_reqd == CLB_READ_ERR)
    {
        /* Abort if we can't load an image */
        DEBUGF("Couldn't load '%s' (%zu)\n", img_path, buf_reqd);
    }
    else
    {
        DEBUGF("Not enough skin buffer: need %zd more.\n",
                buf_reqd - skin_buffer_freespace());
    }

    return -1;
#endif/* !__PCTOOL__ */
}

static bool load_skin_bitmaps(struct wps_data *wps_data, char *bmpdir)
{
    struct skin_token_list *list;
    bool retval = true; /* return false if a single image failed to load */

    /* regular images */
    list = SKINOFFSETTOPTR(skin_buffer, wps_data->images);
    while (list)
    {
        struct wps_token *token = SKINOFFSETTOPTR(skin_buffer, list->token);
        if (!token) goto skip;
        struct gui_img *img = (struct gui_img*)SKINOFFSETTOPTR(skin_buffer, token->value.data);

        if (img && !img->loaded)
        {
            if (img->using_preloaded_icons)
            {
                img->loaded = true;
                token->type = SKIN_TOKEN_IMAGE_DISPLAY_LISTICON;
            }
            else
            {
                char path[MAX_PATH];
                int handle;
                strmemccpy(path, img->bm.data, sizeof(path));
                handle = load_skin_bmp(wps_data, img, bmpdir);
                img->buflib_handle = handle;
                img->loaded = img->buflib_handle > 0;

                if (img->loaded)
                {
                    struct skin_token_list *imglist = SKINOFFSETTOPTR(skin_buffer, list->next);
                    img->subimage_height = img->bm.height / img->num_subimages;
                    struct bitmap* loaded_bm = &img->bm;
                    while (imglist)
                    {
                        token = SKINOFFSETTOPTR(skin_buffer, imglist->token);
                        if (token) {
                            img = (struct gui_img*)SKINOFFSETTOPTR(skin_buffer, token->value.data);
                            if (img && img->bm.data && !strcmp(path, img->bm.data))
                            {
                                img->loaded = true;
                                img->buflib_handle = handle;
                                img->bm = *loaded_bm;
                                img->subimage_height = img->bm.height / img->num_subimages;
                            }
                        }
                        imglist = SKINOFFSETTOPTR(skin_buffer, imglist->next);
                    }
                }
                else
                    retval = false;
            }
        }
    skip:
        list = SKINOFFSETTOPTR(skin_buffer, list->next);
    }

#ifdef HAVE_BACKDROP_IMAGE
    wps_data->backdrop_id = skin_backdrop_assign(backdrop_filename, bmpdir, curr_screen);
#endif /* has backdrop support */
    return retval;
}

static bool skin_load_fonts(struct wps_data *data)
{
    /* don't spit out after the first failue to aid debugging */
    int16_t id_array[MAXUSERFONTS];
    int font_count = 0;
    bool success = true;
    struct skin_element *vp_list;
    int font_id;
    /* walk though each viewport and assign its font */
    for(vp_list = SKINOFFSETTOPTR(skin_buffer, data->tree);
        vp_list; vp_list = SKINOFFSETTOPTR(skin_buffer, vp_list->next))
    {
        /* first, find the viewports that have a non-sys/ui-font font */
        struct skin_viewport *skin_vp =
                SKINOFFSETTOPTR(skin_buffer, vp_list->data);
        if (!skin_vp) continue;
        struct viewport *vp = &skin_vp->vp;

        font_id = skin_vp->parsed_fontid;
        if (font_id == 1)
        {   /* the usual case -> built-in fonts */
            vp->font = screens[curr_screen].getuifont();
            continue;
        }
        else if (font_id <= 0)
        {
            vp->font = FONT_SYSFIXED;
            continue;
        }

        /* now find the corresponding skin_font */
        struct skin_font *font = &skinfonts[font_id-2];
        if (!font->name)
        {
            if (success)
            {
                splashf(HZ, str(LANG_FONT_LOAD_ERROR), "(no name)");
                DEBUGF("font %d not specified\n", font_id);
            }
            success = false;
            continue;
        }

        /* load the font - will handle loading the same font again if
         * multiple viewports use the same */
        if (font->id < 0)
        {
            char path[MAX_PATH];
            snprintf(path, sizeof path, FONT_DIR "/%s", font->name);
#ifndef __PCTOOL__
            font->id = font_load_ex(path, 0, skinfonts[font_id-2].glyphs);

#else
                font->id = font_load(path);
#endif
            //printf("[%d] %s -> %d\n",font_id, font->name, font->id);
            id_array[font_count++] = font->id;
        }

        if (font->id < 0)
        {
            splashf(HZ, str(LANG_FONT_LOAD_ERROR), font->name);
            DEBUGF("Unable to load font %d: '%s'\n", font_id, font->name);
            font->name = NULL; /* to stop trying to load it again if we fail */
            success = false;
            continue;
        }

        /* finally, assign the font_id to the viewport */
        vp->font = font->id;
    }
    if (font_count)
    {
        int16_t *font_ids = skin_buffer_alloc(font_count * sizeof(font_ids[0]));
        if (!success || font_ids == NULL)
        {
            while (font_count > 0)
            {
                if(id_array[--font_count] != -1)
                    font_unload(id_array[font_count]);
            }
            data->font_ids = PTRTOSKINOFFSET(skin_buffer, NULL);
            return false;
        }
        memcpy(font_ids, id_array, sizeof(font_ids[0])*font_count);
        data->font_count = font_count;
        data->font_ids = PTRTOSKINOFFSET(skin_buffer, font_ids);
    }
    else
    {
        data->font_count = 0;
        data->font_ids = PTRTOSKINOFFSET(skin_buffer, NULL);
    }
    return success;
}

static int convert_viewport(struct wps_data *data, struct skin_element* element)
{
    struct skin_viewport *skin_vp = skin_buffer_alloc(sizeof(*skin_vp));
    struct screen *display = &screens[curr_screen];

    if (!skin_vp)
        return CALLBACK_ERROR;

    skin_vp->hidden_flags = 0;
    skin_vp->label = PTRTOSKINOFFSET(skin_buffer, NULL);
    skin_vp->is_infovp = false;
    skin_vp->parsed_fontid = 1;
    element->data = PTRTOSKINOFFSET(skin_buffer, skin_vp);
    curr_vp = skin_vp;
    curr_viewport_element = element;
    if (!first_viewport)
        first_viewport = element;

    viewport_set_defaults(&skin_vp->vp, curr_screen);

#if (LCD_DEPTH > 1) || (defined(HAVE_REMOTE_LCD) && (LCD_REMOTE_DEPTH > 1))
    skin_vp->output_to_backdrop_buffer = false;
#endif
#ifdef HAVE_LCD_COLOR
    skin_vp->start_gradient.start = global_settings.lss_color;
    skin_vp->start_gradient.end = global_settings.lse_color;
    skin_vp->start_gradient.text = global_settings.lst_color;
#endif


    struct skin_tag_parameter *param = get_param(element, 0);
    if (element->params_count == 0) /* default viewport */
    {
        if (data->tree < 0) /* first viewport in the skin */
            data->tree = PTRTOSKINOFFSET(skin_buffer, element);
        skin_vp->label = VP_DEFAULT_LABEL;
        return CALLBACK_OK;
    }

    if (element->params_count == 6)
    {
        if (element->tag->type == SKIN_TOKEN_UIVIEWPORT_LOAD)
        {
            skin_vp->is_infovp = true;
            if (isdefault(param))
            {
                skin_vp->hidden_flags = VP_NEVER_VISIBLE;
                skin_vp->label = VP_DEFAULT_LABEL;
            }
            else
            {
                skin_vp->hidden_flags = VP_NEVER_VISIBLE;
                skin_vp->label = param->data.text;
            }
        }
        else
        {
                skin_vp->hidden_flags = VP_DRAW_HIDEABLE|VP_DRAW_HIDDEN;
                skin_vp->label = param->data.text;
        }
        param++;
    }
    /* x */
    if (!isdefault(param))
    {
        skin_vp->vp.x = param->data.number;
        if (param->data.number < 0)
            skin_vp->vp.x += display->lcdwidth;
        else if (param->type == PERCENT)
            skin_vp->vp.x = param->data.number * display->lcdwidth / 1000;
    }
    param++;
    /* y */
    if (!isdefault(param))
    {
        skin_vp->vp.y = param->data.number;
        if (param->data.number < 0)
            skin_vp->vp.y += display->lcdheight;
        else if (param->type == PERCENT)
            skin_vp->vp.y = param->data.number * display->lcdheight / 1000;
    }
    param++;
    /* width */
    if (!isdefault(param))
    {
        skin_vp->vp.width = param->data.number;
        if (param->data.number < 0)
            skin_vp->vp.width = (skin_vp->vp.width + display->lcdwidth) - skin_vp->vp.x;
        else if (param->type == PERCENT)
            skin_vp->vp.width = param->data.number * display->lcdwidth / 1000;
    }
    else
    {
        skin_vp->vp.width = display->lcdwidth - skin_vp->vp.x;
    }
    param++;
    /* height */
    if (!isdefault(param))
    {
        skin_vp->vp.height = param->data.number;
        if (param->data.number < 0)
            skin_vp->vp.height = (skin_vp->vp.height + display->lcdheight) - skin_vp->vp.y;
        else if (param->type == PERCENT)
            skin_vp->vp.height = param->data.number * display->lcdheight / 1000;
    }
    else
    {
        skin_vp->vp.height = display->lcdheight - skin_vp->vp.y;
    }
    param++;
    /* font */
    if (!isdefault(param))
        skin_vp->parsed_fontid = param->data.number;
    if ((unsigned) skin_vp->vp.x >= (unsigned) display->lcdwidth ||
        skin_vp->vp.width + skin_vp->vp.x > display->lcdwidth ||
        (unsigned) skin_vp->vp.y >= (unsigned) display->lcdheight ||
        skin_vp->vp.height + skin_vp->vp.y > display->lcdheight)
        return CALLBACK_ERROR;

    /* Fix x position for RTL languages */
    if (follow_lang_direction && lang_is_rtl())
        skin_vp->vp.x = display->lcdwidth - skin_vp->vp.x - skin_vp->vp.width;

    return CALLBACK_OK;
}

static int skin_element_callback(struct skin_element* element, void* data)
{
    struct wps_data *wps_data = (struct wps_data *)data;
    struct wps_token *token;
    parse_function function = NULL;

    switch (element->type)
    {
        /* IMPORTANT: element params are shared, so copy them if needed
         *            or use then NOW, dont presume they have a long lifespan
         */
        case TAG:
        {
            token = skin_buffer_alloc(sizeof(*token));
            memset(token, 0, sizeof(*token));
            token->type = element->tag->type;
            token->value.data = INVALID_OFFSET;

            if (element->tag->flags&SKIN_RTC_REFRESH)
            {
#if CONFIG_RTC
                curr_line->update_mode |= SKIN_REFRESH_DYNAMIC;
#else
                curr_line->update_mode |= SKIN_REFRESH_STATIC;
#endif
            }
            else
                curr_line->update_mode |= element->tag->flags&SKIN_REFRESH_ALL;

            element->data = PTRTOSKINOFFSET(skin_buffer, token);

            /* Some tags need special handling for the tag, so add them here */
            switch (token->type)
            {
                case SKIN_TOKEN_ALIGN_LANGDIRECTION:
                    follow_lang_direction = 2;
                    break;
                case SKIN_TOKEN_LOGICAL_IF:
                    function = parse_logical_if;
                    break;
                case SKIN_TOKEN_LOGICAL_AND:
                case SKIN_TOKEN_LOGICAL_OR:
                    function = parse_logical_andor;
                    break;
                case SKIN_TOKEN_SUBSTRING:
                    function = parse_substring_tag;
                    break;
                case SKIN_TOKEN_PROGRESSBAR:
                case SKIN_TOKEN_VOLUME:
                case SKIN_TOKEN_BATTERY_PERCENT:
                case SKIN_TOKEN_PLAYER_PROGRESSBAR:
                case SKIN_TOKEN_PEAKMETER_LEFT:
                case SKIN_TOKEN_PEAKMETER_RIGHT:
                case SKIN_TOKEN_LIST_NEEDS_SCROLLBAR:
#ifdef HAVE_RADIO_RSSI
                case SKIN_TOKEN_TUNER_RSSI:
#endif
                    function = parse_progressbar_tag;
                    break;
                case SKIN_TOKEN_SUBLINE_TIMEOUT:
                case SKIN_TOKEN_BUTTON_VOLUME:
                case SKIN_TOKEN_TRACK_STARTING:
                case SKIN_TOKEN_TRACK_ENDING:
                    function = parse_timeout_tag;
                    break;
                case SKIN_TOKEN_LIST_ITEM_TEXT:
                case SKIN_TOKEN_LIST_ITEM_ICON:
                    function = parse_listitem;
                    break;
                case SKIN_TOKEN_DISABLE_THEME:
                case SKIN_TOKEN_ENABLE_THEME:
                case SKIN_TOKEN_DRAW_INBUILTBAR:
                    function = parse_statusbar_tags;
                    break;
                case SKIN_TOKEN_LIST_TITLE_TEXT:
#ifndef __PCTOOL__
                    sb_skin_has_title(curr_screen);
#endif
                    break;
#if (LCD_DEPTH > 1) || (defined(HAVE_REMOTE_LCD) && (LCD_REMOTE_DEPTH > 1))
                case SKIN_TOKEN_DRAWRECTANGLE:
                    function = parse_drawrectangle;
                    break;
#endif
                case SKIN_TOKEN_FILE_DIRECTORY:
                    token->value.i = get_param(element, 0)->data.number;
                    break;
                case SKIN_TOKEN_FILE_TEXT:
                    function = parse_filetext;
                    break;
#ifdef HAVE_BACKDROP_IMAGE
                case SKIN_TOKEN_VIEWPORT_FGCOLOUR:
                case SKIN_TOKEN_VIEWPORT_BGCOLOUR:
                    function = parse_viewportcolour;
                    break;
                case SKIN_TOKEN_IMAGE_BACKDROP:
                    function = parse_image_special;
                    break;
                case SKIN_TOKEN_VIEWPORT_TEXTSTYLE:
                    function = parse_viewporttextstyle;
                    break;
                case SKIN_TOKEN_VIEWPORT_DRAWONBG:
                    curr_vp->output_to_backdrop_buffer = true;
                    backdrop_filename = BACKDROP_BUFFERNAME;
                    wps_data->use_extra_framebuffer = true;
                    break;
#endif
#ifdef HAVE_LCD_COLOR
                case SKIN_TOKEN_VIEWPORT_GRADIENT_SETUP:
                    function = parse_viewport_gradient_setup;
                    break;
#endif
                case SKIN_TOKEN_TRANSLATEDSTRING:
                case SKIN_TOKEN_SETTING:
                    function = parse_setting_and_lang;
                    break;
                case SKIN_TOKEN_VIEWPORT_CUSTOMLIST:
                    function = parse_playlistview;
                    break;
                case SKIN_TOKEN_LOAD_FONT:
                    function = parse_font_load;
                    break;
                case SKIN_TOKEN_VIEWPORT_ENABLE:
                case SKIN_TOKEN_UIVIEWPORT_ENABLE:
                    token->value.data = get_param(element, 0)->data.text;
                    break;
                case SKIN_TOKEN_IMAGE_PRELOAD_DISPLAY:
                case SKIN_TOKEN_IMAGE_DISPLAY_9SEGMENT:
                    function = parse_image_display;
                    break;
                case SKIN_TOKEN_IMAGE_PRELOAD:
                case SKIN_TOKEN_IMAGE_DISPLAY:
                    function = parse_image_load;
                    break;
                case SKIN_TOKEN_LIST_ITEM_CFG:
                    function = parse_listitemviewport;
                    break;
#ifdef HAVE_TOUCHSCREEN
                case SKIN_TOKEN_TOUCHREGION:
                    function = parse_touchregion;
                    break;
                case SKIN_TOKEN_LASTTOUCH:
                    function = parse_lasttouch;
                    break;
#endif
#ifdef HAVE_ALBUMART
                case SKIN_TOKEN_ALBUMART_LOAD:
                    function = parse_albumart_load;
                    break;
#endif
#ifdef HAVE_SKIN_VARIABLES
                case SKIN_TOKEN_VAR_SET:
                case SKIN_TOKEN_VAR_GETVAL:
                case SKIN_TOKEN_VAR_TIMEOUT:
                    function = parse_skinvar;
                    break;
#endif
                default:
                    break;
            }
            if (function)
            {
                if (function(element, token, wps_data) != 0)
                    return CALLBACK_ERROR;
            }
            /* tags that start with 'F', 'I' or 'D' are for the next file */
            if ( *(element->tag->name) == 'I' || *(element->tag->name) == 'F' ||
                 *(element->tag->name) == 'D')
                token->next = true;
            else if ( token->type == SKIN_TOKEN_TRACK_LENGTH && *(element->tag->name) == 'P')
                token->next = true;

            if (follow_lang_direction > 0 )
                follow_lang_direction--;
            break;
        }
        case VIEWPORT:
            return convert_viewport(wps_data, element);
        case LINE:
        {
            curr_line = skin_buffer_alloc(sizeof(*curr_line));
            curr_line->update_mode = SKIN_REFRESH_STATIC;
            element->data = PTRTOSKINOFFSET(skin_buffer, curr_line);
        }
        break;
        case LINE_ALTERNATOR:
        {
            struct line_alternator *alternator = skin_buffer_alloc(sizeof(*alternator));
            alternator->current_line = 0;
#ifndef __PCTOOL__
            alternator->next_change_tick = current_tick;
#endif
            element->data = PTRTOSKINOFFSET(skin_buffer, alternator);
        }
        break;
        case CONDITIONAL:
        {
            struct conditional *conditional = skin_buffer_alloc(sizeof(*conditional));
            conditional->last_value = -1;
            conditional->token = element->data;
            element->data = PTRTOSKINOFFSET(skin_buffer, conditional);
            if (!check_feature_tag(element->tag->type))
            {
                return FEATURE_NOT_AVAILABLE;
            }
            return CALLBACK_OK;
        }
        case TEXT:
            curr_line->update_mode |= SKIN_REFRESH_STATIC;
            break;
        default:
            break;
    }
    return CALLBACK_OK;
}

/* to setup up the wps-data from a format-buffer (isfile = false)
   from a (wps-)file (isfile = true)*/
bool skin_data_load(enum screen_type screen, struct wps_data *wps_data,
                    const char *buf, bool isfile, struct skin_stats *stats)
{
    char *wps_buffer = NULL;
    if (!wps_data || !buf)
        return false;
    int i;
    for (i=0;i<MAXUSERFONTS;i++)
    {
        skinfonts[i].id = -1;
        skinfonts[i].name = NULL;
    }
#ifdef DEBUG_SKIN_ENGINE
    if (isfile && debug_wps)
    {
        DEBUGF("\n=====================\nLoading '%s'\n=====================\n", buf);
    }
#endif

    _stats = stats;
    skin_clear_stats(stats);
    /* get buffer space from the plugin buffer */
    size_t buffersize = 0;
    wps_buffer = (char *)plugin_get_buffer(&buffersize);

    if (!wps_buffer)
        return false;

    skin_data_reset(wps_data);
    wps_data->wps_loaded = false;
    curr_screen = screen;
    curr_line = NULL;
    curr_vp = NULL;
    curr_viewport_element = NULL;
    first_viewport = NULL;

    if (isfile)
    {
        int fd = open_utf8(buf, O_RDONLY);

        if (fd < 0)
            return false;
        /* copy the file's content to the buffer for parsing,
           ensuring that every line ends with a newline char. */
        unsigned int start = 0;
        while(read_line(fd, wps_buffer + start, buffersize - start) > 0)
        {
            start += strlen(wps_buffer + start);
            if (start < buffersize - 1)
            {
                wps_buffer[start++] = '\n';
                wps_buffer[start] = 0;
            }
        }
        close(fd);
        if (start <= 0)
            return false;
        start++;
        skin_buffer = &wps_buffer[start];
        buffersize -= start;
    }
    else
    {
        skin_buffer = wps_buffer;
        wps_buffer = (char*)buf;
    }

    /* align to long */
    ALIGN_BUFFER(skin_buffer, buffersize, sizeof(long));
#ifdef HAVE_BACKDROP_IMAGE
    backdrop_filename = "-";
    wps_data->backdrop_id = -1;
#endif
    /* parse the skin source */
    skin_buffer_init(skin_buffer, buffersize);
    struct skin_element *tree = skin_parse(wps_buffer, skin_element_callback, wps_data);
    wps_data->tree = PTRTOSKINOFFSET(skin_buffer, tree);
    if (!SKINOFFSETTOPTR(skin_buffer, wps_data->tree)) {
#ifdef DEBUG_SKIN_ENGINE
        if (isfile && debug_wps)
            skin_error_format_message();
#endif
        skin_data_reset(wps_data);
        return false;
    }

    char bmpdir[MAX_PATH];
    if (isfile)
    {
        /* get the bitmap dir */
        char *dot = strrchr(buf, '.');
        strmemccpy(bmpdir, buf, dot - buf + 1);
    }
    else
    {
        snprintf(bmpdir, MAX_PATH, "%s", BACKDROP_DIR);
    }
    /* load the bitmaps that were found by the parsing */
    if (!load_skin_bitmaps(wps_data, bmpdir) ||
        !skin_load_fonts(wps_data))
    {
        skin_data_reset(wps_data);
        return false;
    }
#if defined(HAVE_ALBUMART) && !defined(__PCTOOL__)
    /* last_albumart_{width,height} is either both 0 or valid AA dimensions */
    struct skin_albumart *aa = SKINOFFSETTOPTR(skin_buffer, wps_data->albumart);
    if (aa && (aa->state != WPS_ALBUMART_NONE ||
        (((wps_data->last_albumart_height != aa->height) ||
        (wps_data->last_albumart_width != aa->width)))))
    {
        playback_update_aa_dims();
    }
#endif
#ifndef __PCTOOL__
    wps_data->buflib_handle = core_alloc(skin_buffer_usage());
    if (wps_data->buflib_handle > 0)
    {
        wps_data->wps_loaded = true;
        memcpy(core_get_data(wps_data->buflib_handle), skin_buffer,
                skin_buffer_usage());
        stats->buflib_handles++;
        stats->tree_size = skin_buffer_usage();
    }
#else
    wps_data->wps_loaded = wps_data->tree >= 0;
#endif

#ifdef HAVE_TOUCHSCREEN
    /* Check if there are any touch regions from the skin and not just
     * auto-created ones for bars */
    struct skin_token_list *regions = SKINOFFSETTOPTR(skin_buffer,
            wps_data->touchregions);
    bool user_touch_region_found = false;
    while (regions)
    {
        struct wps_token *token = SKINOFFSETTOPTR(skin_buffer, regions->token);
        struct touchregion *r = NULL;
        if (token)
            r = SKINOFFSETTOPTR(skin_buffer, token->value.data);
        if (r && r->action != ACTION_TOUCH_SCROLLBAR &&
            r->action != ACTION_TOUCH_VOLUME)
        {
            user_touch_region_found = true;
            break;
        }
        regions = SKINOFFSETTOPTR(skin_buffer, regions->next);
    }
    regions = SKINOFFSETTOPTR(skin_buffer, wps_data->touchregions);
    if (regions && !user_touch_region_found)
        wps_data->touchregions = PTRTOSKINOFFSET(skin_buffer, NULL);
#endif

    skin_buffer = NULL;
    return true;
}
