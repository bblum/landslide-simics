/**
 * @file html.h
 * @brief defines for common html formatting codes
 * @author Ben Blum
 */

#ifndef __LS_HTML_H
#define __LS_HTML_H

#define HTML_COLOUR_RED     "#cc0000"
#define HTML_COLOUR_BLUE    "#0000ff"
#define HTML_COLOUR_GREEN   "#00cc00"
#define HTML_COLOUR_MAGENTA "#880088"
#define HTML_COLOUR_YELLOW  "#888800"
#define HTML_COLOUR_CYAN    "#008888"
#define HTML_COLOUR_GREY    "#666666"

#define HTML_COLOUR_START(c) "<span style=\"color: " c ";\">"
#define HTML_COLOUR_END      "</span>"

#define HTML_NEWLINE "<br />\n"

#define HTML_BOX_BEGIN "<table><tr><td>"
#define HTML_BOX_END "</td></tr></table>"

#if 0 // no need for this yet
#define HTML_NBSP "&nbsp;"
#define HTML_TAB  HTML_NBSP HTML_NBSP HTML_NBSP HTML_NBSP \
                  HTML_NBSP HTML_NBSP HTML_NBSP HTML_NBSP
#endif

#endif
