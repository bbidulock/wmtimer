#include <fcntl.h>
#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <sys/wait.h>
#include <sys/param.h>
#include <sys/types.h>

#include <X11/Xlib.h>
#include <X11/xpm.h>
#include <X11/extensions/shape.h>
#include <gtk/gtk.h>

#include "../wmgeneral/wmgeneral.h"
#include "../wmgeneral/misc.h"

#include "wmtimer.xpm"

#define CHAR_WIDTH 5
#define CHAR_HEIGHT 7
#define VERSION "2.92"
#define _MULTI_THREADED

typedef enum {NONE, ALARM, TIMER, TIMER_PAUSED, TIMER_DONE, CHRONO, CHRONO_PAUSED} modeType;
typedef struct {int bell, command;} actionType;
typedef enum {OUT, IN, RETURN} configState;

/*******************************************************************************
 * Functions 
 ******************************************************************************/
// Misc functions
void execAct();
void parseArgs(int argc, char *argv[]);
void processEvent(XEvent *event);
void usage();

// Timer updates
void decrementTimer();
void incrementTimer();

// X11 Screen updates
void blitNum(int num, int x, int y);
void blitString(char *name, int x, int y);
void updateACT();
void updateClock(int clockHour, int clockMin, int clockSec);
void updateMain();

// GTK GUI functions
void *configure(void *);
void callback(GtkWidget *widget, gpointer data);
int delete_event(GtkWidget *widget, GdkEvent *event, gpointer data);
void destroy(GtkWidget *widget, gpointer data);

// Functions to avoid 'implicit declaration' warnings
int atoi();
char toupper();


/*******************************************************************************
 * Globals 
 ******************************************************************************/
static GtkWidget *entry;
static GtkWidget *spinner1;
static GtkWidget *spinner2;
static GtkWidget *spinner3;

int timeSetToZero = 0;
int buttonStatus = -1;
int hour = 0, min = 0, sec = 0;
char *myName;
char command[256];
modeType mode, tmpMode;
actionType action, tmpAction;
configState configSt;


/*******************************************************************************
 * main 
 ******************************************************************************/
int main(int argc, char *argv[])
{
  int prevSec = 0;
  long now;
  struct tm *thisTime;
  int wminet_mask_width = 64;
  int wminet_mask_height = 64;
  char wminet_mask_bits[64 * 64];
  XEvent Event;

  parseArgs(argc, argv);
  gtk_init (&argc, &argv);

  createXBMfromXPM(wminet_mask_bits, wmtimer_xpm, 
      wminet_mask_width, wminet_mask_height);

  openXwindow(argc, argv, wmtimer_xpm, wminet_mask_bits, 
      wminet_mask_width, wminet_mask_height);

  // setMaskXY(-64, 0);

  AddMouseRegion(0, 18, 49, 45, 59);	/* middle button */
  AddMouseRegion(1, 5, 49, 17, 59);	/* left button   */
  AddMouseRegion(2, 46, 49, 59, 59);	/* right button  */
  AddMouseRegion(3, 2, 2, 58, 47);	/* main area     */
  //  AddMouseRegion(3, 6, 2, 60, 18);   	/* first bar     */
  //  AddMouseRegion(4, 6, 20, 60, 34);  	/* second bar    */
  //  AddMouseRegion(5, 6, 37, 60, 48);  	/* third bar     */

  updateMain();
  updateACT(); 

  //  if (hour == 0 && min == 0 && sec == 0) 
  //    timeSetToZero = 1;

  while (1) 
  {
    now = time(0);
    waitpid(0, NULL, WNOHANG);
    thisTime = localtime(&now);
    
    updateClock(thisTime->tm_hour, thisTime->tm_min, thisTime->tm_sec);
    RedrawWindow();

    switch (mode)
    {
      case TIMER:
	if (prevSec < thisTime->tm_sec) 
	{
	  decrementTimer();
	  updateACT();
	  if (hour == 0 && min == 0 && sec == 0 && !timeSetToZero)
	      execAct();
	}
	prevSec = thisTime->tm_sec;
	break;
      case CHRONO:
	if (prevSec < thisTime->tm_sec)
	{
	  incrementTimer();
	  updateACT();
	}
	prevSec = thisTime->tm_sec;
	break;
      case ALARM:
	if (hour == thisTime->tm_hour && 
	    min == thisTime->tm_min && 
	    sec == thisTime->tm_sec) 
	  execAct();
	break;
      case NONE:
      case TIMER_DONE:
      case TIMER_PAUSED:
      case CHRONO_PAUSED:
	break;
    }

    while (XPending(display)) 			// Handle X Events
    {
      XNextEvent(display, &Event);
      processEvent(&Event);
    }

    // Since we have multi-thread, need to detect return from configure
    if (configSt == RETURN)
    {
      configSt = OUT;
      updateMain();
      updateACT(); 
    }

    usleep(100000L);
  }

return 0;
}


/*******************************************************************************
 * execAct 
 ******************************************************************************/
void execAct()
{
  if (action.command && strlen(command))
    execCommand(command);
  if (action.bell)
  {
    printf("\07");
    fflush(stdout);
  }

  if (mode == TIMER)
    mode = TIMER_DONE;		// Don't want to keep doing the Timer event
  else
    mode = NONE;

}


/*******************************************************************************
 * parseArgs 
 ******************************************************************************/
void parseArgs(int argc, char *argv[])
{
  int argIndex;
  int timeDelim = 0;
  int timeParts[] = {0,0,0};
  char *charPtr;

  command[0] = '\0';
  myName = argv[0];

  for (argIndex = 1; argIndex < argc; argIndex++) 
  {
    char *arg = argv[argIndex];

    // Allow for the options that wmgeneral.c accepts
    if (!strcmp(arg, "-color") ||
	!strcmp(arg, "-display") ||
	!strcmp(arg, "-geometry")) {
    }
    else if (*arg == '-') 
    {
      switch (arg[1]) 
      {
	case 'a':
	  mode = ALARM;
	  break;
	case 'c':
	  mode = TIMER;
	  break;
	case 'r':
	  mode = CHRONO;
	  break;
	case 'b':
	  action.bell = 1;
	  break;
	case 'e':
	  strcpy(command, argv[argIndex+1]);
	  action.command = 1;
	  break;
	case 't':
	  if (argv[argIndex+1])
	  {
	    // Check time argument for errors, dont want to segfault on strtok()
	    for (charPtr = argv[argIndex+1]; *charPtr; charPtr++)
	    {
	      if (*charPtr == ':')
		timeDelim++;
	      else 
	      {
		if (timeDelim == 0)
		  timeParts[0]++;
		else if (timeDelim == 1)
		  timeParts[1]++;
		else if (timeDelim == 2)
		  timeParts[2]++;
	      }
	    }

	    // Need to have 2 :'s as time delimiter 
	    // Need to have 1 or 2 digits for each part
	    if (timeDelim != 2 ||	
		!(timeParts[0] == 1 || timeParts[0] == 2) ||
		!(timeParts[1] == 1 || timeParts[1] == 2) ||
		!(timeParts[2] == 1 || timeParts[2] == 2) ) 
	      usage();

	    hour = atoi(strtok(argv[argIndex+1], ":"));
	    min = atoi(strtok(NULL, ":"));
	    sec = atoi(strtok(NULL, ":"));

	    if (hour == 0 && min == 0 && sec == 0) 
	      timeSetToZero = 1;
	  }
	  else
	    usage();
	  break;
	case 'v':
	  printf("WMTimer Version: %s\n", VERSION);
	  _exit(0);
	  break;
	default:
	  usage();
	  break;
      }
    }
  }

}


/*******************************************************************************
 * processEvent 
 ******************************************************************************/
void processEvent(XEvent *event)
{

  int tmpButtonStatus;
  int threadId;
  pthread_t  thread;

  switch (event->type) 
  {
    case Expose:
      RedrawWindow();
      break;
    case DestroyNotify:
      XCloseDisplay(display);
      _exit(0);
      break;
    case ButtonPress:
      tmpButtonStatus = CheckMouseRegion(event->xbutton.x, event->xbutton.y);
      buttonStatus = tmpButtonStatus;
      if (buttonStatus == tmpButtonStatus && buttonStatus >= 0) 
      {
	switch (buttonStatus) 
	{
	  case 0:					// center button
	    break;
	  case 1:					// left arrow button
	    break;
	  case 2:					// right arrow button
	    break;
	  case 3:					// main area
	    break;
	  default:
	    break;
	}
      }
      break;
    case ButtonRelease:
      tmpButtonStatus = CheckMouseRegion(event->xbutton.x, event->xbutton.y);
      if (buttonStatus == tmpButtonStatus && buttonStatus >= 0)
      {
	switch (buttonStatus) 
	{
	  case 0:					// center button
	    if (mode == ALARM) 
	    {
	      hour = min = sec = 0;
	      updateACT();
	    }
	    if (mode == CHRONO)
	      mode = CHRONO_PAUSED;
	    else if (mode == TIMER)
	      mode = TIMER_PAUSED;
	    updateMain();
	    break;
	  case 1:					// left arrow button
	    if (mode == CHRONO)
	      mode = CHRONO_PAUSED;
	    else if (mode == TIMER)
	      mode = TIMER_PAUSED;
	    hour = min = sec = 0;
	    updateACT();
	    updateMain();
	    break;
	  case 2:					// right arrow button
	    if (mode == ALARM)
	    {
	      hour = min = sec = 0;
	      updateACT();
	      timeSetToZero = 0;
	      mode = CHRONO;
	    }
	    else if (mode == TIMER || mode == TIMER_PAUSED)
	      mode = TIMER;
	    else 
	      mode = CHRONO;

	    updateMain();
	    break;
	  case 3:					// main area
	    if (configSt != IN) 	// Dont want to spawn multiple config threads
	    { 
	      threadId = pthread_create(&thread, NULL, configure, NULL);
	      configSt = IN;
	    }
	    break;
	  default:
	    break;
	}
      }
      buttonStatus = -1;
      break;
  }       
}


/*******************************************************************************
 * usage 
 ******************************************************************************/
void usage(void)
{
  fprintf(stderr, "\nWMTimer - Josh King <wmtimer@darkops.net>\n\n");

  fprintf(stderr, "usage: %s -[a|c|r] -t <hh:mm:ss> -e <command>\n\n", myName);

  fprintf(stderr, "    -a     alarm mode, wmtimer will beep/exec command\n");
  fprintf(stderr, "             at specified time\n");
  fprintf(stderr, "    -c     countdowntimer mode, wmtimer will beep/exec\n");
  fprintf(stderr, "	         command when specified time reaches 0 \n");
  fprintf(stderr, "    --color <color> as a word or as rgb:RR/GG/BB\n");
  fprintf(stderr, "    -b     beep\n");
  fprintf(stderr, "    -e     <command> exec command\n");
  fprintf(stderr, "    -r     start in chronograph mode\n");
  fprintf(stderr, "    -t     <hh:mm:ss>\n");
  fprintf(stderr, "    -h     this help screen\n");
  fprintf(stderr, "    -v     print the version number\n");
  fprintf(stderr, "\n");

  _exit(0);
}


/*******************************************************************************
 * decrementTimer 
 ******************************************************************************/
void decrementTimer()
{
  if (!(hour == 0 && min == 0 && sec == 0)) 	// Don't want to go past 0:0:0
    sec--;
  if (sec == -1)
  {
    sec = 59;
    min--;
    if (min == -1)
    {
      min = 59;
      hour--;
    }
  }
}


/*******************************************************************************
 * incrementTimer 
 ******************************************************************************/
void incrementTimer()
{
  sec++;
  if (sec == 60)
  {
    sec = 0;
    min++;
    if (min == 60)
    {
      min = 0;
      hour++;
    }
  }
}


/*******************************************************************************
 * blitNum Blits a number at given co-ordinates
 ******************************************************************************/
void blitNum(int num, int x, int y)
{
  char buf[1024];
  int newx = x;

  if (num > 99)
    newx -= CHAR_WIDTH;

  if (num > 999)
    newx -= CHAR_WIDTH;

  sprintf(buf, "%02i", num);
  blitString(buf, newx, y);
}


/*******************************************************************************
 * blitString Blits a string at given co-ordinates
 ******************************************************************************/
void blitString(char *name, int x, int y)
{
  // copyXPMArea(x_get_pos, y_get_pos, x_dist_from_x_pos, y_dist_from_y_pos, 
  //     x_placement_pos, y_placement_pos);
  // each char/num is 6u wide & 8u high, nums are 64u down, chars are 74u down

  int i;
  int c;
  int k;
  k = x;

  for (i = 0; name[i]; i++)
  {
    c = toupper(name[i]);
    if (c >= 'A' && c <= 'Z')			// its a letter
    {
      c -= 'A';
      copyXPMArea(c * 6, 74, 6, 8, k, y);
      k += 6;
    }
    else 					// its a number or symbol
    {
      c -= '0';
      copyXPMArea(c * 6, 64, 6, 8, k, y);
      k += 6;
    }
  }

}


/*******************************************************************************
 * updateACT  (AlarmChronoTimer)
 ******************************************************************************/
void updateACT()
{
  blitNum(hour, 7, 36);
  blitString(":", 20, 36);
  blitNum(min, 25, 36);
  blitString(":", 38, 36);
  blitNum(sec, 43, 36);
}


/*******************************************************************************
 * updateClock
 ******************************************************************************/
void updateClock(int clockHour, int clockMin, int clockSec)
{
  blitNum(clockHour, 7, 5);
  blitString(":", 20, 5);
  blitNum(clockMin, 25, 5);
  blitString(":", 38, 5);
  blitNum(clockSec, 43, 5);

}


/*******************************************************************************
 * updateMain 
 ******************************************************************************/
void updateMain()
{
  copyXPMArea(13 * 6, 64, 8 * 6, 8, 6, 21);

  switch (mode)
  {
    case ALARM:
      blitString("ALARM:", 13, 21);
      break;
    case TIMER_PAUSED:
    case TIMER:
      blitString("TIMER:", 13, 21);
      break;
    case CHRONO_PAUSED:
    case CHRONO:
      blitString("CHRONO:", 12, 21);
      break;
    default:
      blitString("WMTIMER", 10, 21);
      break;
  }
}


/*******************************************************************************
 * callback 
 ******************************************************************************/
void callback(GtkWidget * widget, gpointer data)
{
  if ((char *) data == "alarm_button")
    tmpMode = ALARM;
  else if ((char *) data == "timer_button")
    tmpMode = TIMER;
  else if ((char *) data == "chrono_button")
    tmpMode = CHRONO;
  else if ((char *) data == "bell_button")
  {
    if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (widget)))
      tmpAction.bell = 1;
    else
      tmpAction.bell = 0;
  }
  else if ((char *) data == "command_button")
  {
    if (gtk_toggle_button_get_active (GTK_TOGGLE_BUTTON (widget))) 
    {
      tmpAction.command = 1;
      gtk_entry_set_editable(GTK_ENTRY (entry), TRUE);
      gtk_entry_set_text(GTK_ENTRY (entry), command);
    }
    else 
    {
      tmpAction.command = 0;
      gtk_entry_set_text(GTK_ENTRY (entry), "");
      gtk_entry_set_editable(GTK_ENTRY (entry), FALSE);
    }
  }
  else if ((char *) data == "ok")
  {
    if (tmpAction.command)
      strcpy(command, gtk_entry_get_text(GTK_ENTRY (entry)));

    hour = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON (spinner1));
    min = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON (spinner2));
    sec = gtk_spin_button_get_value_as_int(GTK_SPIN_BUTTON (spinner3));
    timeSetToZero = 0;

    // If users presses 'ok' and we have not explicitly set the mode, 
    // then it was left on ALARM
    if (!tmpMode)
      tmpMode = ALARM;

    mode = tmpMode;
    // need to reset so that it can default to ALARM if timer not specified 
    tmpMode = NONE;
    action.bell = tmpAction.bell;
    action.command = tmpAction.command;
    configSt = RETURN;
  }
  else if (!strcmp((char *) data, "clear"))
  {
    command[0] = '\0';
    gtk_spin_button_set_value(GTK_SPIN_BUTTON (spinner1), 0);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON (spinner2), 0);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON (spinner3), 0);
    gtk_entry_set_text(GTK_ENTRY (entry), command);
  }
  else if (!strcmp ((char *) data, "cancel"))
    configSt = OUT;
}


/*******************************************************************************
 * delete_event This callback quits the program 
 ******************************************************************************/
int delete_event(GtkWidget * widget, GdkEvent * event, gpointer data)
{
  return(FALSE);
}


/*******************************************************************************
 * destroy destroys the window 
 ******************************************************************************/
void destroy(GtkWidget * widget, gpointer data)
{
  gtk_main_quit();
}


/*******************************************************************************
 * configure
 ******************************************************************************/
void *configure(void *arg)
{
  GtkWidget *window;
  GtkWidget *frame;
  GtkWidget *button;
  GtkWidget *alarm_button;
  GtkWidget *timer_button;
  GtkWidget *chrono_button;
  GtkWidget *box1;
  GtkWidget *box2;
  GtkWidget *sub_vbox;
  GtkWidget *label;
  GtkAdjustment *adj;


  // Create a new window
  window = gtk_window_new (GTK_WINDOW_TOPLEVEL);
  gtk_window_set_title (GTK_WINDOW (window), "Configure");
  gtk_window_set_wmclass (GTK_WINDOW (window), "wmtimerconf", "");

  // Set a handler for delete_event that immediately exits GTK.
  gtk_signal_connect (GTK_OBJECT (window), "destroy",
      GTK_SIGNAL_FUNC (destroy), NULL);

  // Sets the border width of the window.
  gtk_container_set_border_width (GTK_CONTAINER (window), 10);

  // Create Vertical box
  box1 = gtk_vbox_new (FALSE, 0);
  // Add vertical box to main window
  gtk_container_add (GTK_CONTAINER (window), box1);
  gtk_widget_show (box1);

  frame = gtk_frame_new ("Mode");
  gtk_box_pack_start (GTK_BOX (box1), frame, TRUE, TRUE, 2);
  gtk_widget_show (frame);

  box2 = gtk_hbox_new (FALSE, 0);
  gtk_container_add (GTK_CONTAINER (frame), box2);

  // Create Alarm radio button
  alarm_button = gtk_radio_button_new_with_label (NULL, "Alarm");
  gtk_signal_connect (GTK_OBJECT (alarm_button), "clicked",
      GTK_SIGNAL_FUNC (callback), (gpointer) "alarm_button");
  gtk_box_pack_start (GTK_BOX (box2), alarm_button, FALSE, FALSE, 2);
  gtk_widget_show (alarm_button);

  // Create Timer radio button
  timer_button = gtk_radio_button_new_with_label (gtk_radio_button_group
      (GTK_RADIO_BUTTON (alarm_button)), "Timer");
  gtk_signal_connect (GTK_OBJECT (timer_button), "clicked",
      GTK_SIGNAL_FUNC (callback), (gpointer) "timer_button");
  gtk_box_pack_start (GTK_BOX (box2), timer_button, FALSE, FALSE, 2);
  gtk_widget_show (timer_button);
  gtk_widget_show (box2);

  // Create Chrono radio button
  chrono_button = gtk_radio_button_new_with_label (gtk_radio_button_group
      (GTK_RADIO_BUTTON (alarm_button)), "Chrono");
  gtk_signal_connect (GTK_OBJECT (chrono_button), "clicked",
      GTK_SIGNAL_FUNC (callback), (gpointer) "chrono_button");
  gtk_box_pack_start (GTK_BOX (box2), chrono_button, FALSE, FALSE, 2);
  gtk_widget_show (chrono_button);
  gtk_widget_show (box2);

  // If we are in timer mode, set toggle accordingly else default to alarm 
  // Set the button corresponding to the current mode
  if (mode == TIMER || mode == TIMER_PAUSED || mode == TIMER_DONE)
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (timer_button), TRUE);
  else if (mode == CHRONO || mode == CHRONO_PAUSED)
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (chrono_button), TRUE);
  else
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (alarm_button), TRUE);

  // Create frame for the time 
  frame = gtk_frame_new ("Time");
  gtk_box_pack_start (GTK_BOX (box1), frame, TRUE, TRUE, 2);
  gtk_widget_show (frame);

  box2 = gtk_hbox_new (FALSE, 0);
  gtk_container_add (GTK_CONTAINER (frame), box2);

  // Create hours spinner
  adj = (GtkAdjustment *) gtk_adjustment_new (hour, 0.0, 23.0, 1.0, 2.0, 0.0);
  spinner1 = gtk_spin_button_new (adj, 0, 0);
  gtk_spin_button_set_wrap (GTK_SPIN_BUTTON (spinner1), TRUE);
  gtk_spin_button_set_numeric (GTK_SPIN_BUTTON (spinner1), TRUE);
  gtk_box_pack_start (GTK_BOX (box2), spinner1, FALSE, FALSE, 2);
  gtk_widget_show (spinner1);

  // Create separator label
  label = gtk_label_new (" : ");
  gtk_misc_set_alignment (GTK_MISC (label), 0, 0);
  gtk_box_pack_start (GTK_BOX (box2), label, FALSE, FALSE, 2);
  gtk_widget_show (label);

  // Create minutes spinner
  adj = (GtkAdjustment *) gtk_adjustment_new (min, 0.0, 59.0, 1.0, 5.0, 0.0);
  spinner2 = gtk_spin_button_new (adj, 0, 0);
  gtk_spin_button_set_wrap (GTK_SPIN_BUTTON (spinner2), TRUE);
  gtk_spin_button_set_numeric (GTK_SPIN_BUTTON (spinner2), TRUE);
  gtk_box_pack_start (GTK_BOX (box2), spinner2, FALSE, FALSE, 2);
  gtk_widget_show (spinner2);

  // Create separator label
  label = gtk_label_new (" : ");
  gtk_misc_set_alignment (GTK_MISC (label), 0, 0);
  gtk_box_pack_start (GTK_BOX (box2), label, FALSE, FALSE, 2);
  gtk_widget_show (label);

  // Create seconds spinner
  adj = (GtkAdjustment *) gtk_adjustment_new (sec, 0.0, 59.0, 1.0, 5.0, 0.0);
  spinner3 = gtk_spin_button_new (adj, 0, 0);
  gtk_spin_button_set_wrap (GTK_SPIN_BUTTON (spinner3), TRUE);
  gtk_spin_button_set_numeric (GTK_SPIN_BUTTON (spinner3), TRUE);
  gtk_box_pack_start (GTK_BOX (box2), spinner3, FALSE, FALSE, 2);
  gtk_widget_show (spinner3);
  gtk_widget_show (box2);

  // Create frame for 2 buttons and text entry box
  frame = gtk_frame_new ("Action");
  gtk_box_pack_start (GTK_BOX (box1), frame, TRUE, TRUE, 2);
  gtk_widget_show (frame);

  // Create vertical box
  sub_vbox = gtk_vbox_new (FALSE, 0);
  gtk_container_add (GTK_CONTAINER (frame), sub_vbox);
  gtk_widget_show (sub_vbox);

  // Create horizontal box
  box2 = gtk_hbox_new (FALSE, 0);
  gtk_box_pack_start (GTK_BOX (sub_vbox), box2, TRUE, TRUE, 2);

  // Create Bell check button
  button = gtk_check_button_new_with_label ("System Bell");
  gtk_signal_connect (GTK_OBJECT (button), "clicked",
      GTK_SIGNAL_FUNC (callback), (gpointer) "bell_button");
  gtk_box_pack_start (GTK_BOX (box2), button, FALSE, FALSE, 2);
  gtk_widget_show (button);
  
  // If bell mode is active, toggle the button
  if (action.bell)
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (button), TRUE);

  // Create Command check button
  button = gtk_check_button_new_with_label ("Command");
  gtk_signal_connect (GTK_OBJECT (button), "clicked",
      GTK_SIGNAL_FUNC (callback), (gpointer) "command_button");
  gtk_box_pack_start (GTK_BOX (box2), button, FALSE, FALSE, 2);
  gtk_widget_show (button);
  gtk_widget_show (box2);

  // Create horizontal box
  box2 = gtk_hbox_new (FALSE, 0);
  gtk_box_pack_start (GTK_BOX (sub_vbox), box2, TRUE, TRUE, 2);


  // Create label for text entry box 
  label = gtk_label_new ("Command: ");
  gtk_misc_set_alignment (GTK_MISC (label), 0, 0);
  gtk_box_pack_start (GTK_BOX (box2), label, FALSE, FALSE, 2);
  gtk_widget_show (label);

  // Create "Command" text entry area
  entry = gtk_entry_new_with_max_length (100);
  gtk_entry_set_editable (GTK_ENTRY (entry), FALSE);
  gtk_signal_connect (GTK_OBJECT (entry), "activate",
      GTK_SIGNAL_FUNC (callback), entry);
  gtk_box_pack_start (GTK_BOX (box2), entry, FALSE, FALSE, 2);
  gtk_widget_show (entry);
  gtk_widget_show (box2);

  // If command mode is active, toggle the button and allow user to enter a 
  // command
  if (action.command)
    gtk_toggle_button_set_active (GTK_TOGGLE_BUTTON (button), TRUE);

  box2 = gtk_hbox_new (FALSE, 0);
  gtk_box_pack_start (GTK_BOX (box1), box2, TRUE, TRUE, 2);

  // Create "Cancel" button
  button = gtk_button_new_with_label ("Cancel");
  gtk_signal_connect (GTK_OBJECT (button), "clicked",
      GTK_SIGNAL_FUNC (callback), "cancel");
  gtk_signal_connect_object (GTK_OBJECT (button), "clicked",
      GTK_SIGNAL_FUNC (gtk_widget_destroy), GTK_OBJECT (window));
  gtk_box_pack_start (GTK_BOX (box2), button, TRUE, TRUE, 2);
  gtk_widget_show (button);

  // Create "Clear" button
  button = gtk_button_new_with_label ("Clear");
  gtk_signal_connect (GTK_OBJECT (button), "clicked",
      GTK_SIGNAL_FUNC (callback), "clear");
  gtk_box_pack_start (GTK_BOX (box2), button, TRUE, TRUE, 2);
  gtk_widget_show (button);

  // Create "Ok" button
  button = gtk_button_new_with_label ("Ok");
  gtk_signal_connect (GTK_OBJECT (button), "clicked",
      GTK_SIGNAL_FUNC (callback), "ok");
  gtk_signal_connect_object (GTK_OBJECT (button), "clicked",
      GTK_SIGNAL_FUNC (gtk_widget_destroy), GTK_OBJECT (window));
  gtk_box_pack_start (GTK_BOX (box2), button, TRUE, TRUE, 2);
  gtk_widget_show (button);

  gtk_widget_show (box2);

  gtk_widget_show (window);

  gtk_main();

  return 0;
}

