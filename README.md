SlitScan v. 1.0 Beta by NickDoom the Industrial Programmer (sorry for that "hardcore embedded C" code style).

How to build:

1) Download and install OpenWatcom C++ 1.8 (warning: 1.9 can sometimes misread the calling conventions from the project files);
2) Double-click SlitScan.wpj to open the project;
3) Click "Build Target" icon.
4) Rename .DLL to .VDF.

Yeah, it's really THAT simple!

How to use:

1) Click "Video", "Filters", "Add", choose "SlitScan". Read "Beta restrictions" part.
2) Select checkboxes according to your .RAW map file format, set correct Width and Height.
What is .RAW map?
It's a simple image file which can be done from screenshot or from scratch. It contains the Slit Map: a set of individual time delays of each pixel. Zero brightness is equal to "no delay", 1 is "delay by one frame" etc, up to 255 frames delay (8-bit images) or 4095 frames delay (16-bit images. Not "16 bit RGB", but 16 bit per EACH channel!). However, big delay values will probably cause memory error on Beta even though memory-saving frame delay buffer is already implemented. Higher bits of 16 are unusable and must be set to zero (e. g. no pixels brighter than 4095 of 65535 are allowed).
There can be one channel (Slit Map only) and four channels (Slit Map and Preview). Preview is not necessary, but allows to select the Main Timeline area more easily. There are no pixel value restrictions for Preview, of course. You can use, for example, a screenshot. Four channels may have either ARGB or RGBA order, where Alpha contains the Slit Map itself.
3) Load an image. Slit Map will be drawn in weird palette (sorry for Beta). If there is no Preview in the file, no Frame Preview will be shown until at least one frame is processed. Press Esc to exit to Virtual Dub menu.
4) You can return to Settings as needed. Click Time Map Preview to watch the Slit Map (remember palette can be slightly misinforming because of 256-to-0 overflow) or click Frame Preview to watch the last source frame entered the algorithm. You can set Zero Time position manually by typing it and pressing Enter or simply click on the part of the image you want to select as "now". This part will be selected for audio sync.
5) Video resolution may differ from Map resolution. It can be useful for regular patterns which can be bigger than video. If Map is too small, only lower-left area will be processed, other part will have zero delay as if there were zero pixels on the Map.

GPL Restrictions:

This software can't be used as an essential part of non-GPL software. You can base your GPL work on it. You can base your dynamic library on it. That library may work for both GPL and proprietary general-purpose video editors. But you can't statically link this library with non-GPL software, and you also can't make a non-GPL software which main purpose is calling this library. "zOMG a cool effect for just $4.95 (also download this library for free)" will be considered a copyright violation.
