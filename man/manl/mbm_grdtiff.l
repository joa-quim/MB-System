.TH mbm_grdtiff l "10 November 1999" "MB-System 4.6" "MB-System 4.6"
.SH NAME
\fBmbm_grdtiff\fP - Create an executable shellscript which will generate
a TIFF image of gridded data in a GMT grd file.

.SH VERSION
Version 4.6

.SH SYNOPSIS
\fBmbm_grdtiff\fP \fB-I\fP\fIfile\fP 
[\fB-A\fP\fImagnitude[/azimuth/elevation]\fP 
\fB-G\fP\fIcolor_mode\fP \fB-H\fP \fB-K\fP\fIintensity_file\fP 
\fB-O\fP\fIroot\fP \fB-S\fP\fI[color/shade]\fP
\fB-V\fP \fB-W\fP\fIcolor_style[/pallette[/ncolors]]\fP ]

Additional Options:
.br
[\fB-D\fP\fIflipcolor/flipshade\fP
\fB-MGS\fP\fIscalefactor\fP \fB-Q\fP \fB-R\fP\fIw/e/s/n\fP 
\fB-X\fP \fB-Y\fP \fB-Z\fP\fImin/max\fP]

.SH DESCRIPTION
\fBmbm_grdtiff\fP is a macro to generate a shellscript 
which, when executed, will 
generate a geographically located TIFF image of gridded 
data. The primary purpose of this macro is to allow the simple, 
semi-automated production of a nice looking image with a few 
command line arguments. This image can be loaded into the 
ArcInfo and ArcView GIS packages as geographically located 
coverages. Several styles of images can be generated, including 
color fill and shaded relief maps. The available options mirror 
a subset of the options in mbm_grdplot, allowing users to easily 
generate equivalent Postscript plots and TIFF images of gridded 
data. 

.SH AUTHORSHIP
David W. Caress (caress@mbari.org)
.br
  Monterey Bay Aquarium Research Institute
.br
Dale N. Chayes (dale@ldeo.columbia.edu)
.br
  Lamont-Doherty Earth Observatory

.SH SIMPLE DESCRIPTION OF BASIC OPTIONS
.TP
.B \-A
\fImagnitude[/azimuth/elevation]\fP
.br
Sets the parameters which control the
synthetic illumination of the gridded data (shaded relief).
The value \fImagnitude\fP
is an effective vertical exageration which modulates the intensity of
the shading; typical values are in the 0.1 to 10 range.  
The value \fIazimuth\fP
is the azimuth in degrees from north from which the data is illuminated.
The value \fIelevation\fP is the elevation of the illumination 
in degrees from horizontal.
Defaults: \fImagnitude\fP = 0.2; \fIazimuth\fP = 0.0; 
\fIelevation\fP = 30.0;
.TP
.B \-C
\fIcontour_control\fP
.br
If \fB-C\fP is given alone, it causes unannotated contours
to be drawn using a contour interval calculated from the
data. The user may also use \fIcontour_control\fP 
to specify the contour interval. See the COMPLETE DESCRIPTION OF OPTIONS
section below for a more complete discussion.
.TP
.B \-D
\fI[flipcolor/flipshade]\fP
.br
Normally, the color or grayscale tables used for color maps run from
cool colors (or dark grays) for low grid values 
to hot colors (or light grays) for high grid values.
This option reverses the color table so that cool colors (dark grays)
correspond to high values and hot colors (light grays) to low values.
If \fB-D\fP is given alone, it applies to the color table used
for color or gray fill plots,  shaded or unshaded. If the plot
is to be shaded,  either by synthetic illumination (\fB-G\fP\fI2\fP) 
or using an intensity file (\fB-K\fP and \fB-G\fP\fI3\fP options), then
setting \fIflipshade\fP = 1 will cause the shading convention
to be reversed (e.g. high intensities overlaid as light shading).
Using \fB-D\fP\fI0/1\fP will flip the shading convention
but leave the default color convention.
.TP
.B \-G
\fIcolor_mode\fP
.br
Turns on color fill plot and sets the style of the plot.
.br
        \fIcolor_mode\fP = 1: Color/gray fill.
        \fIcolor_mode\fP = 2: Color/gray fill shaded by 
                        synthetic illumination.
        \fIcolor_mode\fP = 3: Color/gray fill shaded by 
                        an intensity file. The \fB-K\fP 
                        option must be used to specify
 			the intensity file.
        \fIcolor_mode\fP = 4: Color/gray fill of slope 
 			magnitude.
        \fIcolor_mode\fP = 5: Color/gray fill shaded by 
                        slope magnitude.
.TP
.B \-H
This "help" flag cause the program to print out a description
of its operation and then exit immediately.
.TP
.B \-I
\fIgrdfile\fP
.br
Sets the name of the gridded data file to be plotted.  
Alternatively, \fIgrdfile\fP may be a list of grid files
(one filename on each line) to be plotted together.
.TP
.B \-K
\fIintensity_file\fP
.br
Sets the name of the gridded data file containing
instensity values to be used for shading the map.  
Alternatively, \fIgrdfile\fP may be a list of grid files
(one filename on each line) to be used together. If a
list of file is supplied, the intensity files must
conform in order to the data grid files they will shade.
.TP
.B \-O
\fIroot\fP
.br
Sets the root used to construct the filename of the output shellscript
(\fIroot\fP.cmd) and names of files created when the shellscript is 
run.  Normally the 
name of the input grid file or grid file list is 
used as the \fIroot\fP. 
.TP
.B \-P
\fIpagesize\fP
.br
This option sets the size of the page the plot will be centered
on. If the user does not set the plot scale, the plot will be
sized as large as will fit on the designated page. If the user
sets the plot scale such that the plot will not fit on the
designated page, a larger page will be used.
The supported page sizes include ANSI A, B, C, D, E,
F, and E1, as well as most metric page sizes. See the
COMPLETE DESCRIPTION OF OPTIONS section 
below for a complete list of 
the supported page sizes. The default page size is A.
.TP
.B \-S
\fI[color/shade]\fP
.br
This option enables effective histogram equalization of the
color and/or shading of the gridded data. The equalization is
not achieved by changing the data values, but rather by
constructing the color or shading tables so that
the boundaries in the tables encompass equal fractions of
the datapoints. This serves to focus color or shading contrasts
in value ranges corresponding to the bulk of the data values.
If \fB-S\fP is given alone or with \fIcolor\fP = 1,
it enables equalization of the color table used
for color or gray fill plots,  shaded or unshaded. If the plot
is to be shaded,  either by synthetic illumination (\fB-G\fP\fI2\fP) 
or using an intensity file (\fB-K\fP and \fB-G\fP\fI3\fP options), then
setting \fIshade\fP = 1 will cause the shading to be equalized.
Using \fB-S\fP\fI0/1\fP will equalize the shading without
equalizing the color table.
.TP
.B \-U
\fIorientation\fP
.br
Normally the orientation of the plot (portrait or landscape)
is selected automatically so as to maximize the plot scale.
The \fB-U\fP option allows the user to set the plot orientation. If
\fIorientation\fP = 1, a portrait plot will be produced; if
\fIorientation\fP = 2, a landscape plot will be produced.
.TP
.B \-V
Causes \fBmbm_grdtiff\fP to operate in "verbose" mode 
so that it outputs
more information than usual.
.TP
.B \-W
\fIcolor_style\fP[\fI/pallette\fP[\fIncolors\fP]]
.br
This option controls the color scheme used for color
fill plots. If \fIcolor_style\fP = 1 [default], then
the color scheme used will be a continuous grading
of colors. If \fIcolor_style\fP = 2, the color scheme
will be a set of discrete color intervals. The color
pallette used is set using \fIpallette\fP. Five pallettes
are available:
 	\fIpallette\fP = 1:	Haxby colors [default]
 	\fIpallette\fP = 2:	high Intensity colors
 	\fIpallette\fP = 3:	low Intensity colors
 	\fIpallette\fP = 4:	grayscale
 	\fIpallette\fP = 5:	uniform grayscale
.br
A complete description of the color pallettes is given
in the COMPLETE DESCRIPTION OF OPTIONS section below.
 
The \fIncolors\fP parameter sets the number of color
values used in plotting, whether the colors are 
represented in a continuous color scale or a
stepped, discrete color scale [default is 11].

.SH COMPLETE DESCRIPTION OF OPTIONS
.TP
.B \-A
\fImagnitude[/azimuth]\fP
.br
Sets the parameters which control the
synthetic illumination of the gridded data (shaded relief).
The value \fImagnitude\fP
is an effective vertical exageration which modulates the intensity of
the shading; typical values are in the 0.1 to 0.5 range.  
The value \fIazimuth\fP
is the azimuth from which the data is illuminated.
Defaults: \fImagnitude\fP = 0.2; \fIazimuth\fP = 0.0;
.TP
.B \-B
\fItickinfo\fP
.br
Sets map boundary tickmark intervals. See the \fBpsbasemap\fP
manual page for details. By default the program chooses
basemap annotations based on the map boundaries.
.TP
.B \-C
\fIcontour_control\fP
.br
If \fB-C\fP is given alone, it causes unannotated contours
to be drawn using a contour interval calculated from the
data. If \fIcontour_control\fP is given, it specifies
the contours to be drawn in one of 
three possible ways:
 
If \fIcontour_control\fP has the suffix ".cpt" and 
can be opened as a file, it is assumed
to be a color palette table.  The color boundaries 
are then used as contour levels.
If the cpt-file has anotation flags in the last 
column then those contours will be
anotated.  By default no contours are labeled; use 
\fB\-MCA\fP to anotate all contours.
 
If \fIcontour_control\fP is a file but not a 
cpt-file, it is expected to contain contour levels
in column 1 and a C(ontour) OR A(nnotate) in col 2. 
The levels marked C (or c) are contoured,
the levels marked A (or a) are contoured and 
annotated.  Optionally, a third column may be
present and contain the fixed anotation angle for 
this contour level.
 
If no file is found, then \fIcontour_control\fP is
interpreted as a constant contour interval.  
 
If a file is given and \fB\-MCT\fP is set, 
then only contours marked with upper case 
C or A will have tickmarks.
.TP
.B \-D
\fI[flipcolor/flipshade]\fP
.br
Normally, the color or grayscale tables used for color maps run from
cool colors (or dark grays) for low grid values 
to hot colors (or light grays) for high grid values.
This option reverses the color table so that cool colors (dark grays)
correspond to high values and hot colors (light grays) to low values.
If \fB-D\fP is given alone, it applies to the color table used
for color or gray fill plots,  shaded or unshaded. If the plot
is to be shaded,  either by synthetic illumination (\fB-G\fP\fI2\fP) 
or using an intensity file (\fB-K\fP and \fB-G\fP\fI3\fP options), then
setting \fIflipshade\fP = 1 will cause the shading convention
to be reversed (e.g. high intensities overlaid as light shading).
Using \fB-D\fP\fI0/1\fP will flip the shading convention
but leave the default color convention.
.TP
.B \-G
\fIcolor_mode\fP
.br
Turns on color fill plot and sets the style of the plot.
.br
        \fIcolor_mode\fP = 1: Color/gray fill.
        \fIcolor_mode\fP = 2: Color/gray fill shaded by 
                        synthetic illumination.
        \fIcolor_mode\fP = 3: Color/gray fill shaded by 
                        an intensity file. The \fB-K\fP 
                        option must be used to specify
 			the intensity file.
        \fIcolor_mode\fP = 4: Color/gray fill of slope 
 			magnitude.
        \fIcolor_mode\fP = 5: Color/gray fill shaded by 
                        slope magnitude.
.br
See the \fBgrdimage\fP manual page for information on shading
with intensity files
.TP
.B \-H
This "help" flag cause the program to print out a description
of its operation and then exit immediately.
.TP
.B \-I
\fIgrdfile\fP
.br
Sets the name of the gridded data file to be plotted.  
The data must be in a form acceptable to \fBGMT\fP version 3
programs (see the \fBGMT\fP Cookbook & Technical Reference).
Alternatively, \fIgrdfile\fP may be a list of grid files
(one filename on each line) to be plotted together. This
is useful when data from a region is broken up into several
grid files rather than a single very large grid file.
.TP
.B \-J
\fIprojection\fP[\fI/scale | /width\fP]
.br
Selects the map projection. By default the map projection is
Mercator and the plot scale is chosen to fit on the selected
page size (see \fB-P\fP option). The user may specify a
different projection to be used, in which case the plot scale
is still automatically chosen to fit the page. The user may
also specify both the projection and the plot scale. If
the projection specifying character is upper case, a plot
width rather than a plot scale is used.
The scale values are specified in inch/degree or in 1:xxxxx
ratios. Plot widths are specified in inches. If the user
specifies a plot scale such that the plot will not fit
on the default A size page, a appropriately larger page
size will be chosen.
.br
.sp
\fBCYLINDRICAL PROJECTIONS:\fP
.br
.sp
\fB\-Jc\fP\fIlon0/lat0/scale\fP (Cassini)
.br
\fB\-Jm\fP\fIscale\fP (Mercator)
.br
\fB\-Joa\fP\fIlon0/lat0/azimuth/scale\fP (Oblique Mercator - point and azimuth)
.br
\fB\-Job\fP\fIlon0/lat0/lon1/lat1/scale\fP (Oblique Mercator - two points)
.br
\fB\-Joc\fP\fIlon0/lat0/lonp/latp/scale\fP (Oblique Mercator - point and pole)
.br
\fB\-Jq\fP\fIlon0/scale\fP (Equidistant Cylindrical Projection (Plate Carree))
.br
\fB\-Jt\fP\fIlon0/scale\fP (TM - Transverse Mercator)
.br
\fB\-Ju\fP\fIzone/scale\fP (UTM - Universal Transverse Mercator)
.br
\fB\-Jy\fP\fIlon0/lats/scale\fP (Basic Cylindrical Projection) 
.br
.sp
\fBAZIMUTHAL PROJECTIONS:\fP
.br
.sp
\fB\-Ja\fP\fIlon0/lat0/scale\fP (Lambert).
.br
\fB\-Je\fP\fIlon0/lat0/scale\fP (Equidistant).
.br
\fB\-Jg\fP\fIlon0/lat0/scale\fP (Orthographic).
.br
\fB\-Js\fP\fIlon0/lat0/scale\fP (General Stereographic)
.br
.sp
\fBCONIC PROJECTIONS:\fP
.br
.sp
\fB\-Jb\fP\fIlon0/lat0/lat1/lat2/scale\fP (Albers)
.br
\fB\-Jl\fP\fIlon0/lat0/lat1/lat2/scale\fP (Lambert)
.br
.sp
\fBMISCELLANEOUS PROJECTIONS:\fP
.br
.sp
\fB\-Jh\fP\fIlon0/scale\fP (Hammer)
.br
\fB\-Ji\fP\fIlon0/scale\fP (Sinusoidal)
.br
\fB\-Jk\fP\fIlon0/scale\fP (Eckert VI)
.br
\fB\-Jn\fP\fIlon0/scale\fP (Robinson)
.br
\fB\-Jr\fP\fIlon0/scale\fP (Winkel Tripel)
.br
\fB\-Jw\fP\fIlon0/scale\fP (Mollweide)
.br
.sp
\fBNON-GEOGRAPHICAL PROJECTIONS:\fP
.br
.sp
\fB\-Jp\fP\fIscale\fP (Linear projection for polar (theta,r) coordinates)
.br
\fB\-Jx\fP\fIx-scale\fP[\fBl|p\fP\fIpow\fP][\fI/y-scale\fP[\fBl|p\fP\fIpow\fP]] (Linear, log, and power scaling)
.br
More details can be found in the \fBpsbasemap\fP manpages.
.TP
.B \-K
\fIintensity_file\fP
.br
Sets the name of the gridded data file containing
instensity values to be used for shading the map.  
Alternatively, \fIgrdfile\fP may be a list of grid files
(one filename on each line) to be used together. If a
list of files is supplied, the intensity files must
conform in order to the list of data grid files they will shade.
.TP
.B \-L
\fItitle:scalelabel\fP
.br
Sets the title and the label for the colorscale (if used) of
the plot. Note that a colon (:) rather than a slash (/) is
used to separate the labels. Colons cannot be used in the
labels themselves. If this option is not used, then a default title
and colorscale label are provided. If the title is supplied
alone, a default colorscale label will be provided. To force
no title use \fB-L\fP" "; to force no title or colorscale
label use \fB-L\fP" : ".
.TP
.B \-M
A series of "miscellaneous" options are provided which are
given as \fB-M\fP followed by a two character identifier, followed
by any other parameters associated with that option.
The \fB-M\fP options may be strung together separated by
colons, e.g. "-MGQ100:GU:CA200/10", which is equivalent to
"-MGQ -MGU -MCA200/10". 
.TP
.B \-MGD
\fIgmtdef/value\fP
.br
Allows the user to set the \fBGMT\fP default values used as
the plot is constructed. This command may be given repeatedly
to set as many \fBGMT\fP defaults as required. For example, to
set the basemap annotation font to Courier, use 
"-MGDANOT_FONT/Courier".
.TP
.B \-MGF
\fIscale_loc\fP
.br
Sets the location of the color scale. The possible values
of \fIscale_loc\fP are:
 	\fIscale_loc\fP = b:	bottom of plot
 	\fIscale_loc\fP = t:	top of plot
 	\fIscale_loc\fP = l:	left of plot
 	\fIscale_loc\fP = r:	right of plot
.br
[Default \fIscale_loc\fP = b]
.TP
.B \-MGL
[\fIf\fP][\fIx\fP]\fIlon0/lat0/slat/length\fP[\fIm\fP]
.br
Draws a simple map scale centered on \fIlon0/lat0\fP.  Use \fB\-Lx\fP to specify position in inch instead.
Scale is calculated at latitude \fIslat\fP, \fIlength\fP is in km [miles if m is appended].
Use \fB\-Lf\fP to get a "fancy" scale [Default is plain].
.TP
.B \-MGQ
\fIdpi\fP
.br
Sets the resolution in dots per inch of the raster image used
for color fill maps. Larger values of \fIdpi\fP produce larger
Postscript plot files. [Default is 100].
.TP
.B \-MGS
\fIscalefactor\fP
.br
The gridded data is multiplied by \fIscalefactor\fP.
This option is most often used flip the sign of the
data (\fIscalefactor\fP = -1). [Default no scaling]
.TP
.B \-MGT
\fIx/y/size/angle/font/just/text\fP
.br
Causes a text label to plotted on the map.
\fIsize\fP is text size in points, \fIangle\fP is 
measured in degrees counter-clockwise from horizontal, 
\fIfontno\fP sets the font type, \fIjustify\fP sets the alignment. 
If \fIfontno\fP starts with a leading hyphen, then 
the remainder of \fIfontno\fP is taken to be a 
textstring with the desired fontname.  See the 
\fBgmtdefaults\fP man page for names and numbers of available fonts 
(or run \fBpstext \-L\fP).  The alignment 
number refers to the part of the textstring that will be mapped 
onto the (\fIx,y\fP) point: 1 = Lower Left corner, 
2 = Lower Center, 3 = Lower Right, 5 = Mid Left, 6 = Mid Center, 
7 = Mid Right, 9 = Upper Left, 10 = Upper Center, 
11 = Upper Right. This option may be given as many times as
needed.
.TP
.B \-MGU
\fP[\fI/dx/dy/\fP][\fIlabel\fP]
.br
Draw Unix System time stamp on plot.  User may specify where the lower left corner
of the stamp should fall on the page relative to lower left corner of plot in inch [Default is (-0.75,-0.75)].  Optionally,
append a label, or \fBc\fP (which will plot the command string.)
.TP
.B \-MCA
\fIanot_int/\fP[\fIffont_size\fP][\fIaangle\fP][\fI/r/g/b\fP][\fIo\fP]]
.br
\fIanot_int\fP is annotation interval in data units.  
Ignored if contour levels are given in a file.
[Default is no annotations].  Several options 
can be set to modify the form of the annotation.  
Append f\fIfont_size\fP to change font size [9], 
append \fI/r/g/b\fP to change color of text fill box
[PAGE_COLOR], append a\fIangle\fP to fix 
annotation angle [Default follows contour], 
and append 'o' to draw the outline of the 
surrounding text box [Default is no outline].
.TP
.B \-MCG
\fIgap/width\fP \fB-MCQ\fP\fIcut \fP
.br
\fIgap\fP is distance in inch between each 
annotation along the same contour.
\fIwidth\fP is number of points over which to 
estimate the best fitting slope
for contour labels  [Default is 4/10].
.TP
.B -MCQ
\fIcut\fP
.br
Do not draw contours with less than \fIcut\fP number 
of points [Draw all contours].
.TP
.B \-MCT
[\fI+|-\fP][\fIgap/length\fP][\fI:LH\fP] 
.br
Will draw tickmarks pointing in the downward direction every \fIgap\fP along the innermost closed contours.  Append \fIgap\fP and tickmark length (in inch) or use defaults [0.2/0.05].
User may choose to tick only local highs or local lows by specifying \fB\-T+\fP or \fB\-T-\fP, respectively.  Appending :LH will plot the characters L and H at the center of closed
innermost contours (local lows and highs).  L and H can be any single character (e.g., LH, -+, etc.)
If a file is given by \fB\-C\fP and \fB\-T\fP is set, 
then only contours marked with upper case C or A will have tickmarks [and anotation].
.TP
.B \-MCW
\fItype\fP[\fIpen\fP]
.br
\fItype\fP, if present, can be 'a' for annotated contours or 'c' for regular contours [Default].
\fIpen\fP sets the attributes for the particular line.  Default values for annotated contours:
width = 3, color = black, texture = solid.  Regular contours have default width = 1.
.TP
.B \-MXG
\fIfill\fP 
.br
Select filling of symbols for xy plotting.  
Set the shade (0\-255) or color 
(r/g/b) [Default is no fill]. To reset no fill,
use \fIfill\fP = "N".
For polygons, you may optionally specify 
\fB\-Gp\fP\fIicon_size/pattern\fP, where 
\fIpattern\fP gives the number of the
image pattern (1-32) OR the name of a 
icon-format file.  \fIicon_size\fP sets 
the unit size in inch.
To invert black and white pixels, use 
\fB\-GP\fP instead of \fB\-Gp\fP.  See 
\fBGMTs\fP Cookbook & Technical Reference
Appendix E for information on individual patterns.
.TP
.B \-MXI
\fIxy_file \fP
.br
Specifies a file containing (x,y) pairs to be plotted
as lines or symbols. The line and symbol characteristics
are set using the last \fB-MXG\fP, \fB-MXS\fP, and \fB-MXW\fP
options used. All of the \fB-MX\fP commands can be
given multiple times, so by stringing series of these
commands together the user can plot different files
using different line or symbol characteristics.
[Default is a solid black line].
.TP
.B \-MXS
\fIsymbol/size\fP 
.br
Selects symbol to be used for plotting the next xy data
file. Setting \fIsymbol\fP = "N" causes line plotting.
Choose between:
.TP
.B \-MXSa
st\fBa\fPr.  \fIsize\fP is radius of circumscribing circle.
.TP
.B \-MXSb
\fBb\fPar extending from \fIbase\fP to y.  \fIsize\fP is bar width.  By default,
\fIbase\fP = 0.  Append /\fIbase\fP to change this value.  Append \fBu\fP if \fIsize\fP
is in x-units [Default is inch].
.TP
.B \-MXSc
\fBc\fPircle.  \fIsize\fP is diameter of circle.
.TP
.B \-MXSd
\fBd\fPiamond.  \fIsize\fP is side of diamond.
.TP
.B \-MXSe
\fBe\fPllipse.  Direction (in degrees counterclockwise from horizontal), major_axis (in inch), and minor_axis (in inch) must be found in columns 3, 4, and 5.
.TP
.B \-MXSf
\fBf\fPault.  Give distance gap between ticks and ticklength in inch.  If gap is
negative, it is interpreted to mean number of ticks instead.  Append \fBl\fP or \fBr\fP to draw tick on
the left or right side of line [Default is centered].  Upper case \fBL\fP or \fBR\fP draws a triangle
instead of line segment.
.TP
.B \-MXSh
\fBh\fPexagon.  Give side in inch.
.TP
.B \-MXSi
\fBi\fPnverted triangle.  Give side in inch.
.TP
.B \-MXSl
\fBl\fPetter or text string.  Give size in inch, and append /\fIstring\fP after the size.  Note that the size is only approximate; no individual scaling
is done for different characters.  Remember to escape special characters like *.
.TP
.B \-MXSp
\fBp\fPoint.  No size needs to be specified (1 pixel is used).
.TP
.B \-MXSs
\fBs\fPquare.  Give side in inch.
.TP
.B \-MXSt
\fBt\fPriangle.  Give side in inch.
.TP
.B \-MXSv
\fBv\fPector.  Direction (in degrees counterclockwise from horizontal) and length (in inch) must be found in columns 3 and 4.  \fIsize\fP,
if present, will be interpreted as arrowwidth/headlength/headwidth (in inch) [Default is 0.03/0.12/0.1 inch].
By default arrow attributes remains invariant to the length
of the arrow.  To have the size of the vector scale down with decreasing size,
append n\fPnorm\fP, where vectors shorter than \fInorm\fP will have their
attributes scaled by length/\fInorm\fP.
.TP
.B \-MXSV
Same as \fB\-MXSv\fP, except azimuth (in degrees east of north) should be given instead of direction.  The azimuth will
be mapped into an angle based on the chosen map projection (\fB\-MXSv\fP leaves the directions
unchanged.)
.TP
.B \-MXSx
cross.  Give length in inch.
.TP
.B \-MXW
\fIpen\fP
.br
Set pen attributes for xy plotting. See chapter 4.12 in the
GMT Technical reference for a discussion of GMT pen values.  
[Defaults: width = 1, color = 0/0/0, 
texture = solid]. 
.TP
.B \-O
\fIroot\fP
.br
Sets the root used to construct the filename of the output shellscript
(\fIroot\fP.cmd) and names of files created when the shellscript is 
run.  Normally the 
name of the input grid file or grid file list is 
used as the \fIroot\fP. 
.TP
.B \-P
\fIpagesize\fP
.br
This option sets the size of the page the plot will be centered
on. If the user does not set the plot scale, the plot will be
sized as large as will fit on the designated page. If the user
sets the plot scale such that the plot will not fit on the
designated page, a larger page will be used.
The supported page sizes are:

          American ANSI sizes:
          A     8.5 x 11.0 in.    ( 215.9 x  279.4 mm)
          B    11.0 x 17.0 in.    ( 279.4 x  431.8 mm)
          C    17.0 x 22.0 in.    ( 431.8 x  558.8 mm)
          D    22.0 x 34.0 in.    ( 558.8 x  863.6 mm)
          E    34.0 x 44.0 in.    ( 863.6 x 1117.6 mm)
          F    28.0 x 40.0 in.    ( 711.2 x 1016.0 mm)
          E1   44.0 x 68.0 in.    (1117.6 x 1727.2 mm)

          Metric ISO A sizes:
          A0   841.0 x 1189.0 mm  (33.11 x 46.81 in.)
          A1   594.0 x  841.0 mm  (23.39 x 33.11 in.)
          A2   420.0 x  594.0 mm  (16.54 x 23.39 in.)
          A3   297.0 x  420.0 mm  (11.69 x 16.54 in.)
          A4   210.0 x  297.0 mm  ( 8.27 x 11.69 in.)
          A5   148.0 x  210.0 mm  ( 5.83 x  8.27 in.)
          A6   105.0 x  148.0 mm  ( 4.13 x  5.83 in.)
          A7    74.0 x  105.0 mm  ( 2.91 x  4.13 in.)
          A8    52.0 x   74.0 mm  ( 2.05 x  2.91 in.)
          A9    37.0 x   52.0 mm  ( 1.46 x  2.05 in.)
          A10   26.0 x   37.0 mm  ( 1.02 x  1.46 in.)

          Metric ISO B sizes:
          B0   1000.0x 1414.0 mm  (39.37 x 55.67 in.)
          B1   707.0 x 1000.0 mm  (27.83 x 39.37 in.)
          B2   500.0 x  707.0 mm  (19.68 x 27.83 in.)
          B3   353.0 x  500.0 mm  (13.90 x 19.68 in.)
          B4   250.0 x  353.0 mm  ( 9.84 x 13.90 in.)
          B5   176.0 x  250.0 mm  ( 6.93 x  9.84 in.)
          B6   125.0 x  176.0 mm  ( 4.92 x  6.93 in.)
          B7    88.0 x  125.0 mm  ( 3.46 x  4.92 in.)
          B8    62.0 x   88.0 mm  ( 2.44 x  3.46 in.)
          B9    44.0 x   62.0 mm  ( 1.73 x  2.44 in.)
          B10   31.0 x   44.0 mm  ( 1.22 x  1.73 in.)

          Metric ISO C sizes:
          C0   914.4 x 1300.5 mm  (36.00 x 51.20 in.)
          C1   650.2 x  914.4 mm  (25.60 x 36.00 in.)
          C2   457.2 x  650.2 mm  (18.00 x 25.60 in.)
          C3   325.1 x  457.2 mm  (12.80 x 18.00 in.)
          C4   228.6 x  325.1 mm  ( 9.00 x 12.80 in.)
          C5   162.6 x  228.6 mm  ( 6.40 x  9.00 in.)
          C6   114.3 x  162.6 mm  ( 4.50 x  6.40 in.)
          C7    81.3 x  114.3 mm  ( 3.20 x  4.50 in.)

The default page size is A.
.TP
.B \-Q
Normally, the output plot generation shellscript 
includes lines which execute
a program to display the Postscript image on the screen.  
This option causes those lines to be commented out so
that executing the shellscript produces a Postscript plot
but does not attempt to display it on the screen.
The program
to be used to display the Postscript is set 
using \fBmbdefaults\fP;
the default value can be overridden by setting the environment
variable $MB_PS_VIEWER. 
.TP
.B \-R
\fIwest/east/south/north\fP
.br
\fIwest, east, south,\fP and \fInorth\fP specify the Region of interest.  To specify boundaries
in degrees and minutes [and seconds], use the dd:mm[:ss] format.  Append \fBr\fP if lower left and upper right
map coordinates are given instead of wesn.
You may ask for a larger \fIw/e/s/n\fP region to have more room between the image and the axes.
A smaller region than specified in the grdfile will result in a subset of the grid [Default is
region given by the grdfile].
.TP
.B \-S
\fI[color/shade]\fP
.br
This option enables effective histogram equalization of the
color and/or shading of the gridded data. The equalization is
not achieved by changing the data values, but rather by
constructing the color or shading tables so that
the boundaries in the tables encompass equal fractions of
the datapoints. This serves to focus color or shading contrasts
in value ranges corresponding to the bulk of the data values.
If \fB-S\fP is given alone or with \fIcolor\fP = 1,
it enables equalization of the color table used
for color or gray fill plots,  shaded or unshaded. If the plot
is to be shaded,  either by synthetic illumination (\fB-G\fP\fI2\fP) 
or using an intensity file (\fB-K\fP and \fB-G\fP\fI3\fP options), then
setting \fIshade\fP = 1 will cause the shading to be equalized.
Using \fB-S\fP\fI0/1\fP will equalize the shading without
equalizing the color table.
.TP
.B \-U
\fIorientation\fP
.br
Normally the orientation of the plot (portrait or landscape)
is selected automatically so as to maximize the plot scale.
The \fB-U\fP option allows the user to set the plot orientation. If
\fIorientation\fP = 1, a portrait plot will be produced; if
\fIorientation\fP = 2, a landscape plot will be produced.
.TP
.B \-V
Causes \fBmbm_grdtiff\fP to operate in "verbose" mode 
so that it outputs
more information than usual.
.TP
.B \-W
\fIcolor_style\fP[\fI/pallette\fP[\fIncolors\fP]]
.br
This option controls the color scheme used for color
fill plots. If \fIcolor_style\fP = 1 [default], then
the color scheme used will be a continuous grading
of colors. If \fIcolor_style\fP = 2, the color scheme
will be a set of discrete color intervals. The color
pallette used is set using \fIpallette\fP. Seven pallettes
are available:
 	\fIpallette\fP = 1:	Haxby colors [default]
 	\fIpallette\fP = 2:	high Intensity colors
 	\fIpallette\fP = 3:	low Intensity colors
 	\fIpallette\fP = 4:	grayscale
 	\fIpallette\fP = 5:	uniform grayscale
 	\fIpallette\fP = 6:	uniform black
 	\fIpallette\fP = 7:	uniform white

The RGB definitions of the color pallettes are:

color pallette 1 - Haxby Color Table
  red:   255 255 255 255 240 205 138 106  50  40  37
  green: 255 186 161 189 236 255 236 235 190 127  57
  blue:  255 133  68  87 121 162 174 255 255 251 175

color pallette 2 - High Intensity Colors
  red:   255 255 255 255 128   0   0   0   0 128 255
  green:   0  64 128 255 255 255 255 128   0   0   0
  blue:    0   0   0   0   0   0 255 255 255 255 255

color pallette 3 - Low Intensity Colors
  red:   200 194 179 141  90   0   0   0   0  90 141
  green:   0  49  90 141 179 200 141  90   0   0   0
  blue:    0   0   0   0   0   0 141 179 200 179 141

color pallette 4 - Grayscale
  red:   255 230 204 179 153 128 102  77  51  26   0
  green: 255 230 204 179 153 128 102  77  51  26   0
  blue:  255 230 204 179 153 128 102  77  51  26   0

color pallette 5 - Uniform Grayscale
  red:   128 128 128 128 128 128 128 128 128 128 128
  green: 128 128 128 128 128 128 128 128 128 128 128
  blue:  128 128 128 128 128 128 128 128 128 128 128

color pallette 6 - Uniform Black
  red:     0   0   0   0   0   0   0   0   0   0   0
  green:   0   0   0   0   0   0   0   0   0   0   0
  blue:    0   0   0   0   0   0   0   0   0   0   0

color pallette 7 - Uniform White
  red:   255 255 255 255 255 255 255 255 255 255 255
  green: 255 255 255 255 255 255 255 255 255 255 255
  blue:  255 255 255 255 255 255 255 255 255 255 255
 
The Haxby colors have been adapted from a pallette
developed by Dr. William Haxby of the Lamont-Doherty
Earth Observatory; this pallette is pleasing to the
eye and well suited for shading. The high intensity
colors describe linear paths through RGB space from
red to blue to green to purple; because the colors are high
intensity they are not well suited to shading.
The low intensity colors are similar to the high
intensity, but muted and thus well suited to shading.
The grayscale pallette runs linearly from white to
black and is commonly used for plots of sidescan and amplitude
data. The uniform grayscale is useful for non-color
shaded relief plots.
 
The \fIncolors\fP parameter sets the number of color
values used in plotting, whether the colors are 
represented in a continuous color scale or a
stepped, discrete color scale [default is 11].
.TP
.B \-X
Normally, \fBmbm_grdtiff\fP creates an executable shellscript and
then exits.  This option will cause the shellscript to be executed
in the background before \fBmbm_grdtiff\fP exits.
.TP
.B \-Y
Normally, \fBmbm_grdtiff\fP generates nicely rounded numbers
for the boundaries of the color pallette. Often, the resulting
color bounds extend well outside the range of the gridded data.
This option causes
the color boundaries to be uniformly distributed between the
minimum and maximum values of the grid.
.TP
.B \-Z
\fImin/max\fP
.br
This option overrides the minimum and maximum values of
the gridded data, affecting the color pallette and the
contour interval if those parameters are not specified
by the user.

.SH EXAMPLES
Suppose we have obtained two GRD files with dimensions of
127 by 194, one containing gridded bathymetry
(grd_sb2112_example_bath) and the other gridded sidescan
(grd_sb2112_example_ss). In order to generate a shellscript
which will in turn generate a contour plot of the bathymetry,
the following will suffice:
 
  	mbm_grdtiff -Igrd_sb2112_example_bath -C \\
  	    -V -Ogrd_sb2112_example_bathcont
 
In order to generate a color fill plot overlaid by
contours, we use both the -G1 and -C options.
Because the data has been gridded as bathymetry (positive
down) rather than as topography (positive up), the default
plot will have "hot" colors for deep regions and "cold"
colors for shallow regions; this is the opposite of the
convention we usually use. In order to fix the colors, we have
to either rescale the data by multiplying the bathymetry
by -1 (accomplished with -MGS-1), or flip the color
pallette (accomplished with -D). We use the latter approach:
 
  	mbm_grdtiff -Igrd_sb2112_example_bath -G1 -C -D \\
  	    -V -Ogrd_sb2112_example_bathfill
 
In order to generate a grayscale plot of the sidescan grid, 
we use -G1 and -W1/4. We also use -D so that high sidescan
amplitudes are shown as dark.
 
  	mbm_grdtiff -Igrd_sb2112_example_ss -G1 -D \\
  	    -V -Ogrd_sb2112_example_ssfill
 
Now consider generating a shaded relief view of the
gridded bathymetry. We choose to illuminate
the bathymetry from the northeast (azimuth of 45 degrees)
and to use a shading magnitude of 0.4 (-A0.4/45).
Because this grid is so small, the default shaded
relief image is likely to be grainy. To fix this problem, we
specify a dots per inch resolution of 72 (-MGQ72); this
will take longer and generate a larger plotfile, but the
plot will look better. We also use the -L option to specify
the title and color scale label for the plot.  We also use 
the -X flag this so that the plot generation shellscript is 
executed immediately. Here is the command:
 
  	mbm_grdtiff -Igrd_sb2112_example_bath \\
  	    -G2 -A0.4/45 -D -MGQ72 -X -V \\
  	    -L"Shaded Relief Bathymetry":"Depth (meters)" \\
  	    -Osb2112_example_bathshade
 
Now, consider generating a plot of the bathymetry 
overlaid with the gridded sidescan.
The sidescan overlay is specified using the -K option.
We want the colors for the bathymetry to be chosen without
histogram equalization, but we also want histogram
equalization to be applied to the sidescan data used for
shading. To do this, we use -S0/1, where the first number
(0) specifies no histogram equalization of the color
scale and the second number (1) causes histogram 
equalization of the shading sidescan data to be
implemented. In order to maintain the convention that
high sidescan amplitudes are black, we flip both the
color pallette (as in the previous example) and the
shading scale with -D1/1. We could also flip the shading
by specifing a negative shading magnitude (-A-0.4).
In this case, we forgo specifying the image resolution,
resulting in a grainy plot:
 
  	mbm_grdtiff -Igrd_sb2112_example_bath \\
  	    -G3 -Kgrd_sb2112_example_ss \\
  	    -S0/1 -D1/1 -A0.4 -X -V \\
  	    -L"Bathymetry Overlaid With Sidescan":"Depth (meters)" \\
  	    -Osb2112_example_bathss
 
As an example, the contents of the plotting shellscript 
"grd_sb2112_example_bathfill.cmd" are:
 
.br
 #
 # Shellscript to create Postscript plot of data in grd file
 # Created by macro mbm_grdtiff
 #
 # This shellscript created by following command line:
 # mbm_grdtiff -Igrd_sb2112_example_bath -G1 -C -D \\
 #	-V -Ogrd_sb2112_example_bathfill
 #
 # Save existing GMT defaults
 echo Saving GMT defaults...
 gmtdefaults -L > gmtdefaults$$
 #
 # Set new GMT defaults
 echo Setting new GMT defaults...
 gmtset ANOT_FONT Helvetica
 gmtset LABEL_FONT Helvetica
 gmtset HEADER_FONT Helvetica
 gmtset ANOT_FONT_SIZE 8
 gmtset LABEL_FONT_SIZE 8
 gmtset HEADER_FONT_SIZE 10
 gmtset FRAME_WIDTH 0.074999999999999997
 gmtset TICK_LENGTH 0.074999999999999997
 gmtset PAGE_ORIENTATION LANDSCAPE
 gmtset COLOR_BACKGROUND 0/0/0
 gmtset COLOR_FOREGROUND 255/255/255
 gmtset COLOR_NAN 255/255/255
 #
 # Make color pallette table file
 echo Making color pallette table file...
 echo   3000 255 255 255   3150 255 186 133 > \\
		grd_sb2112_example_bathfill.cpt
 echo   3150 255 186 133   3300 255 161  68 >> \\
		grd_sb2112_example_bathfill.cpt
 echo   3300 255 161  68   3450 255 189  87 >> \\
		grd_sb2112_example_bathfill.cpt
 echo   3450 255 189  87   3600 240 236 121 >> \\
		grd_sb2112_example_bathfill.cpt
 echo   3600 240 236 121   3750 205 255 162 >> \\
		grd_sb2112_example_bathfill.cpt
 echo   3750 205 255 162   3900 138 236 174 >> \\
		grd_sb2112_example_bathfill.cpt
 echo   3900 138 236 174   4050 106 235 255 >> \\
		grd_sb2112_example_bathfill.cpt
 echo   4050 106 235 255   4200  50 190 255 >> \\
		grd_sb2112_example_bathfill.cpt
 echo   4200  50 190 255   4350  40 127 251 >> \\
		grd_sb2112_example_bathfill.cpt
 echo   4350  40 127 251   4500  37  57 175 >> \\
		grd_sb2112_example_bathfill.cpt
 #
 # Make color image
 echo Running grdimage...
 grdimage grd_sb2112_example_bath -Jm24.418434289993325 \\
.br
        -R114.221/114.421/-31.9001/-31.6377 \\
.br
        -Cgrd_sb2112_example_bathfill.cpt \\
.br
        -P -X1.8081565710006675 -Y2 -K -V \\
.br
        > grd_sb2112_example_bathfill.ps
.br
 #
 # Make contour plot
 echo Running grdcontour...
 grdcontour grd_sb2112_example_bath -Jm24.418434289993325 \\
.br
        -R114.221/114.421/-31.9001/-31.6377 \\
.br
        -C50 \\
.br
        -L3144.51/4499.44 -Wc1p \\
.br
        -P -K -O -V >> grd_sb2112_example_bathfill.ps
 #
.br
 # Make color scale
 echo Running psscale...
 psscale -Cgrd_sb2112_example_bathfill.cpt \\
.br
        -D2.4418/-0.5000/4.8837/0.1500h \\
.br
        -B":.Data Values:" \\
.br
        -P -K -O -V >> grd_sb2112_example_bathfill.ps
.br
 #
 # Make basemap
 echo Running psbasemap...
 psbasemap -Jm24.418434289993325 \\
.br
        -R114.221/114.421/-31.9001/-31.6377 \\
.br
        -B5m/5m:."Data File grd_sb2112_example_bath": \\
.br
        -P -O -V >> grd_sb2112_example_bathfill.ps
.br
 #
 # Delete surplus files
 echo Deleting surplus files...
 rm -f grd_sb2112_example_bathfill.cpt
 #
 # Reset GMT default fonts
 echo Resetting GMT fonts...
 mv gmtdefaults$$ .gmtdefaults
 #
 # Run xpsview
 echo Running xpsview in background...
 xpsview -ps a -maxp 4m grd_sb2112_example_bathfill.ps &
 #
 # All done!
 echo All done!

.SH SEE ALSO
\fBgrdimage\fP(l), \fBgrdcontour\fP(l), 
\fBmbdefaults\fP(l), \fBmbgrid\fP(l), \fBmbsystem\fP(l), 
\fBmbm_grd3dplot\fP(l), \fBmbm_plot\fP(l),
\fBpsbasemap\fP(l), \fBpstext\fP(l), \fBpsxy\fP(l)

.SH BUGS
By making this macro more useful, we have also made it
more complex. 


