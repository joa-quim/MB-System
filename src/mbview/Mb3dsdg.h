
/* Begin user code block <abstract> */
/* End user code block <abstract> */

/**
 * README: Portions of this file are merged at file generation
 * time. Edits can be made *only* in between specified code blocks, look
 * for keywords <Begin user code> and <End user code>.
 */
/*
 * Generated by the ICS Builder Xcessory (BX).
 *
 * BuilderXcessory Version 6.1.3
 * Code Generator Xcessory 6.1.3 (08/19/04) CGX Scripts 6.1 Motif 2.1
 *
 */
#ifndef Mb3dsdg_H
#define Mb3dsdg_H

/**
 * Forward declare the class data pointer type so that it can
 * easily be used as a parameter to class functions and data members.
 */
typedef struct _Mb3dsdgData *Mb3dsdgDataPtr;

/**
 * Globally included information.
 */

/**
 * Class specific includes.
 */

typedef struct _Mb3dsdgData {
	/*
	 * Classes created by this class.
	 */

	/*
	 * Widgets created by this class.
	 */
	Widget Mb3dsdg;
	Widget pushButton_reset;
	Widget scale_timelag;
	Widget scale_snell;
	Widget toggleButton_mouse_panzoom1;
	Widget toggleButton_mouse_rotate1;
	Widget label_mousemode;
	Widget scale_headingbias;
	Widget scale_pitchbias;
	Widget scale_rollbias;
	Widget label_status;
	Widget menuBar;
	Widget cascadeButton_view;
	Widget pulldownMenu_view;
	Widget toggleButton_view_boundingbox;
	Widget separator1;
	Widget toggleButton_view_flagged;
	Widget separator;
	Widget toggleButton_view_noconnect;
	Widget toggleButton_view_connectgood;
	Widget toggleButton_view_connectall;
	Widget separator2;
	Widget toggleButton_view_scalewithflagged;
	Widget cascadeButton_mouse;
	Widget pulldownMenu_mouse;
	Widget toggleButton_mouse_rotate;
	Widget toggleButton_mouse_panzoom;
	Widget cascadeButton_action;
	Widget pulldownMenu_action;
	Widget pushButton_action_applybias;
	Widget separator3;
	Widget pushButton_action_flagsparsevoxels_A;
	Widget pushButton_action_flagsparsevoxels_B;
	Widget pushButton_action_flagsparsevoxels_C;
	Widget pushButton_action_flagsparsevoxels_D;
	Widget pushButton_action_flagsparsevoxels_E;
	Widget pushButton_action_flagsparsevoxels_F;
	Widget separator4;
	Widget cascadeButton_action_colorsoundings;
	Widget pulldownMenu_action_colorsoundings;
	Widget pushButton_action_colorsoundingsblack;
	Widget pushButton_action_colorsoundingsred;
	Widget pushButton_action_colorsoundingsyellow;
	Widget pushButton_action_colorsoundingsgreen;
	Widget pushButton_action_colorsoundingsbluegreen;
	Widget pushButton_action_colorsoundingsblue;
	Widget pushButton_action_colorsoundingspurple;
	Widget separator5;
	Widget cascadeButton_action_optimizebiasvalues;
	Widget pulldownMenu_action_optimizebiasvalues;
	Widget pushButton_action_optimizebiasvalues_r;
	Widget pushButton_action_optimizebiasvalues_p;
	Widget pushButton_action_optimizebiasvalues_h;
	Widget pushButton_action_optimizebiasvalues_rp;
	Widget pushButton_action_optimizebiasvalues_rph;
	Widget pushButton_action_optimizebiasvalues_t;
	Widget pushButton_action_optimizebiasvalues_s;
	Widget cascadeButton_dismiss;
	Widget pulldownMenu_dismiss;
	Widget pushButton_dismiss;
	Widget drawingArea;
	Widget radioBox_soundingsmode;
	Widget toggleButton_mouse_toggle;
	Widget toggleButton_mouse_pick;
	Widget toggleButton_mouse_erase;
	Widget toggleButton_mouse_restore;
	Widget toggleButton_mouse_grab;
	Widget toggleButton_mouse_info;

	/**
	 * All methods and data..
	 */
} Mb3dsdgData;

/*
 * Function: Mb3dsdgCreate()
 *		The creation for class Mb3dsdg.
 */
Mb3dsdgDataPtr Mb3dsdgCreate(Mb3dsdgDataPtr, Widget, String, ArgList, Cardinal);

/**
 * Set routines for exposed resources.
 */
#endif
