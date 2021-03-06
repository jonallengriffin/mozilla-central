#!/usr/bin/python
# vim: set shiftwidth=4 tabstop=8 autoindent expandtab:
# ***** BEGIN LICENSE BLOCK *****
# Version: MPL 1.1/GPL 2.0/LGPL 2.1
#
# The contents of this file are subject to the Mozilla Public License Version
# 1.1 (the "License"); you may not use this file except in compliance with
# the License. You may obtain a copy of the License at
# http://www.mozilla.org/MPL/
#
# Software distributed under the License is distributed on an "AS IS" basis,
# WITHOUT WARRANTY OF ANY KIND, either express or implied. See the License
# for the specific language governing rights and limitations under the
# License.
#
# The Original Code is mark-generate.py .
#
# The Initial Developer of the Original Code is the Mozilla Foundation.
# Portions created by the Initial Developer are Copyright (C) 2008
# the Initial Developer. All Rights Reserved.
#
# Contributor(s):
#   L. David Baron <dbaron@dbaron.org>, Mozilla Corporation (original author)
#
# Alternatively, the contents of this file may be used under the terms of
# either the GNU General Public License Version 2 or later (the "GPL"), or
# the GNU Lesser General Public License Version 2.1 or later (the "LGPL"),
# in which case the provisions of the GPL or the LGPL are applicable instead
# of those above. If you wish to allow use of your version of this file only
# under the terms of either the GPL or the LGPL, and not to allow others to
# use your version of this file under the terms of the MPL, indicate your
# decision by deleting the provisions above and replace them with the notice
# and other provisions required by the GPL or the LGPL. If you do not delete
# the provisions above, a recipient may use your version of this file under
# the terms of any one of the MPL, the GPL or the LGPL.
#
# ***** END LICENSE BLOCK *****

# For general fontforge documentation, see:
#   http://fontforge.sourceforge.net/
# For fontforge scripting documentation, see:
#   http://fontforge.sourceforge.net/scripting-tutorial.html
#   http://fontforge.sourceforge.net/scripting.html
# and most importantly:
#   http://fontforge.sourceforge.net/python.html

# To install what you need, on Ubuntu,
#   sudo apt-get install python-fontforge

import fontforge

# generate a set of fonts, each with our special glyph at one codepoint,
# and nothing else
for codepoint in range(ord("A"), ord("D") + 1):
    for (mark, width) in [("", 1500), ("2", 1800)]:
        charname = chr(codepoint)
        f = fontforge.font()
        n = "Mark" + mark + charname
        f.fontname = n
        f.familyname = n
        f.fullname = n
        f.copyright = "Copyright (c) 2008 Mozilla Corporation"

        g = f.createChar(codepoint, charname)
        g.importOutlines("mark" + mark + "-glyph.svg")
        g.width = width

        f.generate("mark" + mark + charname + ".ttf")
        f.generate("mark" + mark + charname + ".otf")


for codepoint in range(ord("A"), ord("A") + 1):
    for (mark, width) in [("", 1500), ("2", 1800)]:
        for (uposname, upos) in [("low", -350), ("high", -50)]:
            charname = chr(codepoint)
            f = fontforge.font()
            n = "Mark" + mark + charname
            f.fontname = n
            f.familyname = n
            f.fullname = n
            f.descent = 400
            f.upos = upos
            f.uwidth = 100
            f.copyright = "Copyright (c) 2008 Mozilla Corporation"
    
            g = f.createChar(codepoint, charname)
            g.importOutlines("mark" + mark + "-glyph.svg")
            g.width = width
    
            f.generate("mark" + mark + charname + "-" + uposname +
                       "underline.ttf")

# font with a ligature involving a space

f = fontforge.font()
n = "MarkAB-spaceliga"
f.fontname = n
f.familyname = n
f.fullname = n
f.copyright = "Copyright (c) 2008-2011 Mozilla Corporation"

g = f.createChar(ord(" "), "space")
g.width = 1000
for charname in ["A", "B"]:
    g = f.createChar(ord(charname), charname)
    g.importOutlines("mark-glyph.svg")
    g.width = 1500

f.addLookup("liga-table", "gsub_ligature", (), (("liga",(("latn",("dflt")),)),))
f.addLookupSubtable("liga-table", "liga-subtable")
g = f.createChar(-1, "spaceA")
g.glyphclass = "baseligature";
g.addPosSub("liga-subtable", ("space", "A"))
g.importOutlines("mark2-glyph.svg")
g.width = 1800

f.generate("markAB-spaceliga.otf")

# font with a known line-height (which is greater than winascent + windescent).

f = fontforge.font()
lineheight = 1500
n = "MarkA-lineheight" + str(lineheight)
f.fontname = n
f.familyname = n
f.fullname = n
f.copyright = "Copyright (c) 2008-2011 Mozilla Corporation"

g = f.createChar(ord(" "), "space")
g.width = 1000
g = f.createChar(ord("A"), "A")
g.importOutlines("mark-glyph.svg")
g.width = 1500

f.os2_typoascent_add = False
f.os2_typoascent = 800
f.os2_typodescent_add = False
f.os2_typodescent = -200
f.os2_use_typo_metrics = True
f.os2_typolinegap = lineheight - (f.os2_typoascent - f.os2_typodescent)
# glyph height is 800 (hhea ascender - descender)
f.hhea_linegap = lineheight - 800

f.generate("markA-lineheight" + str(lineheight) + ".ttf")
