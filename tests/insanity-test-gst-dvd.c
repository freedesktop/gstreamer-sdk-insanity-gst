/* Insanity QA system

 Copyright (c) 2012, Collabora Ltd
 Author: Vincent Penquerc'h <vincent@collabora.co.uk>

 This program is free software; you can redistribute it and/or
 modify it under the terms of the GNU Lesser General Public
 License as published by the Free Software Foundation; either
 version 2.1 of the License, or (at your option) any later version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 Lesser General Public License for more details.

 You should have received a copy of the GNU Lesser General Public
 License along with this program; if not, write to the
 Free Software Foundation, Inc., 59 Temple Place - Suite 330,
 Boston, MA 02111-1307, USA.
*/

#include <stdio.h>
#include <string.h>
#include <glib.h>
#include <glib-object.h>
#include <gst/interfaces/navigation.h>
#include <insanity-gst/insanity-gst.h>

/* Number of random commands to send to move between menus */
#define MAX_RANDOM_COMMANDS 256

typedef enum {
  NEXT_STEP_NOW,
  NEXT_STEP_ON_PLAYING,
  NEXT_STEP_RESTART_ON_PLAYING,
} NextStepTrigger;

static GstElement *global_pipeline = NULL;
static unsigned int global_state = 0;
static unsigned int global_next_state = 0;
static gboolean global_waiting_on_playing = FALSE;
static guint global_angle = 0;
static guint global_n_angles = 0;
static guint global_n_allowed_commands = 0;
static GstNavigationCommand global_allowed_commands[256];
static guint global_random_command_counter = 0;
static GRand *global_prg = NULL;
static guint global_state_change_timeout = 0;

static void on_ready_for_next_state (InsanityGstPipelineTest *ptest, gboolean timeout);

static GstPipeline*
dvd_test_create_pipeline (InsanityGstPipelineTest *ptest, gpointer userdata)
{
  GstElement *pipeline = NULL, *playbin2 = NULL;
  const char *launch_line = "playbin2 name=foo audio-sink=fakesink";
  GError *error = NULL;

  pipeline = gst_parse_launch (launch_line, &error);
  if (!pipeline) {
    insanity_test_validate_step (INSANITY_TEST (ptest), "valid-pipeline", FALSE,
      error ? error->message : NULL);
    if (error)
      g_error_free (error);
    return NULL;
  }
  else if (error) {
    /* Do we get a dangling pointer here ? gst-launch.c does not unref */
    pipeline = NULL;
    insanity_test_validate_step (INSANITY_TEST (ptest), "valid-pipeline", FALSE,
      error->message);
    g_error_free (error);
    return NULL;
  }

  global_pipeline = pipeline;

  return GST_PIPELINE (pipeline);
}

static NextStepTrigger
send_dvd_command(InsanityGstPipelineTest *ptest, const char *step, guintptr data)
{
  gst_navigation_send_command (GST_NAVIGATION (global_pipeline), data);
  return NEXT_STEP_ON_PLAYING;
}

static NextStepTrigger
retrieve_commands(InsanityGstPipelineTest *ptest, const char *step, guintptr data)
{
  InsanityTest *test = INSANITY_TEST (ptest);
  GstQuery *q;
  gboolean res;
  guint n_commands;
  GstNavigationCommand cmd;
  const char *extra_data_prefix = (const char*)data;

  global_n_allowed_commands = 0;

  q = gst_navigation_query_new_commands ();
  res = gst_element_query (global_pipeline, q);
  if (res) {
    guint current, count;
    res = gst_navigation_query_parse_commands_length (q, &n_commands);
    if (res) {
      if (n_commands <= sizeof(global_allowed_commands)/sizeof(global_allowed_commands[0])) {
        guint n;

        if (extra_data_prefix) {
          GValue v = {0};
          char *label;

          g_value_init (&v, G_TYPE_INT);
          g_value_set_int (&v, n_commands);
          label = g_strdup_printf("commands.%s.n-commands", extra_data_prefix);
          insanity_test_set_extra_info (test, label, &v);
          g_free (label);
          g_value_unset (&v);
        }

        for (n=0; n<n_commands; n++) {
          res = gst_navigation_query_parse_commands_nth (q, n, &cmd);
          if (res) {
            if (extra_data_prefix) {
              GValue v = {0};
              char *label;

              g_value_init (&v, G_TYPE_INT);
              g_value_set_int (&v, cmd);
              label = g_strdup_printf("commands.%s.command.%u", extra_data_prefix, global_n_allowed_commands);
              insanity_test_set_extra_info (test, label, &v);
              g_free (label);
              g_value_unset (&v);
            }
            global_allowed_commands[global_n_allowed_commands++] = cmd;
          }
          else {
            insanity_test_validate_step (test, step, FALSE, "Failed to parse command query result");
            break;
          }
        }
        if (res) {
          insanity_test_validate_step (test, step, TRUE, NULL);
        }
      }
      else {
        insanity_test_validate_step (test, step, FALSE, "Too many commands in command query result");
      }
    }
    else {
      insanity_test_validate_step (test, step, FALSE, "Failed to parse command query result");
    }
  }
  else {
    insanity_test_validate_step (test, step, FALSE, "Failed to send command query");
  }
  gst_query_unref (q);
  return NEXT_STEP_NOW;
}

static NextStepTrigger
retrieve_angles(InsanityGstPipelineTest *ptest, const char *step, guintptr data)
{
  InsanityTest *test = INSANITY_TEST (ptest);
  GstQuery *q;
  gboolean res;
  const char *extra_data_prefix = (const char*)data;

  q = gst_navigation_query_new_angles ();
  res = gst_element_query (global_pipeline, q);
  if (res) {
    guint current, count;
    res = gst_navigation_query_parse_angles (q, &current, &count);
    if (res) {
      if (extra_data_prefix) {
        GValue v = {0};
        char *label;

        g_value_init (&v, G_TYPE_INT);
        g_value_set_int (&v, current);
        label = g_strdup_printf("angles.%s.current", extra_data_prefix);
        insanity_test_set_extra_info (test, label, &v);
        g_free (label);
        g_value_unset (&v);

        g_value_init (&v, G_TYPE_INT);
        g_value_set_int (&v, count);
        label = g_strdup_printf("angles.%s.n-angles", extra_data_prefix);
        insanity_test_set_extra_info (test, label, &v);
        g_free (label);
        g_value_unset (&v);
      }

      global_angle = current;
      global_n_angles = count;

      insanity_test_validate_step (test, step, TRUE, NULL);
    }
    else {
      insanity_test_validate_step (test, step, FALSE, "Failed to parse answer to angles query");
    }
  }
  else {
    insanity_test_validate_step (test, step, FALSE, "Failed to send angles query");
  }
  gst_query_unref (q);
  return NEXT_STEP_NOW;
}

static NextStepTrigger
cycle_angles(InsanityGstPipelineTest *ptest, const char *step, guintptr data)
{
  InsanityTest *test = INSANITY_TEST (ptest);
  guint n;

  /* First retrieve amount of angles, will be saved globally */
  retrieve_angles(ptest, step, (guintptr)NULL);

  /* Then loop through each */
  for (n=global_n_angles; n>0; --n) {
    gst_navigation_send_command (GST_NAVIGATION (global_pipeline), GST_NAVIGATION_COMMAND_NEXT_ANGLE);
  }

  /* Again, other direction */
  for (n=global_n_angles; n>0; --n) {
    gst_navigation_send_command (GST_NAVIGATION (global_pipeline), GST_NAVIGATION_COMMAND_PREV_ANGLE);
  }

  /* Do we end up where we were ? Or do the next/prev stop at 0 and N-1 ? Samples have only 1 angle */

  return NEXT_STEP_NOW;
}

static NextStepTrigger
cycle_unused_commands(InsanityGstPipelineTest *ptest, const char *step, guintptr data)
{
  InsanityTest *test = INSANITY_TEST (ptest);
  guint n, i;

  /* First retrieve allowed commands, will be saved globally */
  retrieve_commands(ptest, step, (guintptr)NULL);

  /* Then loop through each of those which are not allowed */
  for (n=0; n<sizeof(global_allowed_commands)/sizeof(global_allowed_commands[0]); ++n) {
    GstNavigationCommand cmd = (GstNavigationCommand)n;
    gboolean found = FALSE;

    for (i=0; i<global_n_allowed_commands; ++i) {
      if (global_allowed_commands[i] == cmd) {
        found = TRUE;
        break;
      }
    }

    if (!found) {
      gst_navigation_send_command (GST_NAVIGATION (global_pipeline), cmd);
    }
  }

  insanity_test_validate_step (test, "cycle-unused-commands", TRUE, NULL);

  return NEXT_STEP_NOW;
}

static gboolean
state_change_timeout (gpointer data)
{
  InsanityGstPipelineTest *ptest = data;

  on_ready_for_next_state (ptest, TRUE);

  return FALSE;
}

static NextStepTrigger
send_random_commands(InsanityGstPipelineTest *ptest, const char *step, guintptr data)
{
  InsanityTest *test = INSANITY_TEST (ptest);
  GstNavigationCommand cmd;
  guint *counter = (guint*)data;

  /* First retrieve allowed commands, will be saved globally */
  retrieve_commands(ptest, step, (guintptr)NULL);

  /* Then select one random command in the list, and send it */
  cmd = global_allowed_commands[g_rand_int_range (global_prg, 0, global_n_allowed_commands - 1)];
  insanity_test_printf (test, "Sending random command %u (%u/%u)\n", cmd, 1+*counter, MAX_RANDOM_COMMANDS);
  gst_navigation_send_command (GST_NAVIGATION (global_pipeline), cmd);

  /* Now, we can't know in advance whether a command will trigger a state change
     or not. Things like UP, DOWN, will not, but only some MENU* will be active,
     despite being listed as allowed commands.
     Also, I think it's only a convention that UP, DOWN, etc, do not start a
     new movie. So we can't error out if they do.
     Therefore, we add a timeout function that will bump to next when no state
     change has happened after a short time. */

  /* Stop after enough commands */
  global_state_change_timeout = g_timeout_add (1000, (GSourceFunc)&state_change_timeout, ptest);
  if (++*counter == MAX_RANDOM_COMMANDS) {
    *counter = 0;
    insanity_test_validate_step (test, "send-random-commands", TRUE, NULL);
    return NEXT_STEP_ON_PLAYING;
  }
  else {
    return NEXT_STEP_RESTART_ON_PLAYING;
  }
}

/*
  This is an ordered list of steps to perform.
  A test function needs the passed step if needed, and validate it UNLESS it returns
  NEXT_STEP_ON_PLAYING, in which case validation will be automatically done whenever
  next state change to PLAYING happens (eg, selecting a title to play is just sending
  a command, but it is deemed to have succeeded only if we get to PLAYING at some
  point later). A step can return NEXT_STEP_RESTART_ON_PLAYING, which will cause it
  to be called again upon next state change to PLAYING. The step function is then
  responsible for keeping its own private state so it can end up returning something
  else at some point.
*/
static const struct {
  const char *step;
  NextStepTrigger (*f)(InsanityGstPipelineTest*, const char*, guintptr);
  guintptr data;
} steps[] = {
  { "select-root-menu", &send_dvd_command, GST_NAVIGATION_COMMAND_DVD_ROOT_MENU},
  { "retrieve-commands", &retrieve_commands, (guintptr)"root-menu"},
  { "retrieve-angles", &retrieve_angles, (guintptr)"root-menu"},
  { "select-first-menu", &send_dvd_command, GST_NAVIGATION_COMMAND_MENU1},
  { "retrieve-commands", &retrieve_commands, (guintptr)"first-menu"},
  { "retrieve-angles", &retrieve_angles, (guintptr)"first-menu"},
  { "cycle-angles", &cycle_angles, 0},
  { "cycle-unused-commands", &cycle_unused_commands, 0},
  { "select-root-menu", &send_dvd_command, GST_NAVIGATION_COMMAND_DVD_ROOT_MENU},
  { "send-random-commands", &send_random_commands, (guintptr)&global_random_command_counter},
  { "select-root-menu", &send_dvd_command, GST_NAVIGATION_COMMAND_DVD_ROOT_MENU},
};

static gboolean
do_next_step (gpointer data)
{
  InsanityGstPipelineTest *ptest = data;
  InsanityTest *test = data;
  NextStepTrigger next;

  /* When out of steps to perform, end the test */
  if (global_state == sizeof(steps)/sizeof(steps[0])) {
    insanity_test_done (test);
    return FALSE;
  }

  insanity_test_printf(test, "Calling step %u/%zu (%s, data %u)\n",
      global_state+1,sizeof(steps)/sizeof(steps[0]),
      steps[global_state].step, steps[global_state].data);
  next = (*steps[global_state].f)(ptest, steps[global_state].step, steps[global_state].data);
  switch (next) {
    default:
      g_assert (0);
      /* fall through */
    case NEXT_STEP_NOW:
      global_state++;
      g_idle_add ((GSourceFunc)&do_next_step, ptest);
      break;
    case NEXT_STEP_ON_PLAYING:
      global_next_state = global_state+1;
      global_waiting_on_playing = TRUE;
      break;
    case NEXT_STEP_RESTART_ON_PLAYING:
      global_next_state = global_state;
      global_waiting_on_playing = TRUE;
      break;
  }

  return FALSE;
}

static void
on_ready_for_next_state (InsanityGstPipelineTest *ptest, gboolean timeout)
{
  global_waiting_on_playing = FALSE;
  if (global_next_state != global_state) {
    insanity_test_validate_step (INSANITY_TEST (ptest), steps[global_state].step, TRUE, NULL);
    global_state = global_next_state;
  }
  insanity_test_printf(INSANITY_TEST (ptest), "Got %s, going to next step\n",
      timeout ? "timeout" : "playing");
  g_idle_add ((GSourceFunc)&do_next_step, ptest);
}

static gboolean
dvd_test_bus_message (InsanityGstPipelineTest * ptest, GstMessage *msg)
{
  switch (GST_MESSAGE_TYPE (msg)) {
    case GST_MESSAGE_STATE_CHANGED:
      if (GST_MESSAGE_SRC (msg) == GST_OBJECT (global_pipeline)) {
        GstState oldstate, newstate, pending;
        gst_message_parse_state_changed (msg, &oldstate, &newstate, &pending);

        if (global_state_change_timeout) {
          g_source_remove (global_state_change_timeout);
          global_state_change_timeout = 0;
        }

        if (newstate == GST_STATE_PLAYING && pending == GST_STATE_VOID_PENDING && global_waiting_on_playing) {
          on_ready_for_next_state (ptest, FALSE);
        }
      }
      break;
  }

  return TRUE;
}

static gboolean
dvd_test_setup(InsanityTest *test)
{
  GValue v = {0};
  guint32 seed;

  /* Retrieve seed */
  insanity_test_get_argument (test, "seed", &v);
  seed = g_value_get_uint(&v);
  g_value_unset (&v);

  /* Generate one if zero */
  if (seed == 0) {
    seed = g_random_int();
    if (seed == 0) /* we don't really care for bias, we just don't want 0 */
      seed = 1;
  }

  /* save that seed as extra-info */
  g_value_init (&v, G_TYPE_UINT);
  g_value_set_uint (&v, seed);
  insanity_test_set_extra_info (test, "seed", &v);
  g_value_unset (&v);

  global_prg = g_rand_new_with_seed(seed);

  return TRUE;
}

static void
dvd_test_teardown (InsanityTest *test)
{
  (void)test;
  g_rand_free (global_prg);
  global_prg = NULL;
}

static gboolean
dvd_test_start(InsanityTest *test)
{
  InsanityGstPipelineTest *ptest = INSANITY_GST_PIPELINE_TEST (test);
  GValue uri = {0};
  const char *protocol;

  if (!insanity_test_get_argument (test, "uri", &uri))
    return FALSE;
  if (!strcmp (g_value_get_string (&uri), "")) {
    insanity_test_validate_step (test, "valid-pipeline", FALSE, "No URI to test on");
    g_value_unset (&uri);
    return FALSE;
  }

  if (!gst_uri_is_valid (g_value_get_string (&uri))) {
    insanity_test_validate_step (test, "uri-is-dvd", FALSE, NULL);
    g_value_unset (&uri);
    return FALSE;
  }
  protocol = gst_uri_get_protocol (g_value_get_string (&uri));
  if (!protocol || g_ascii_strcasecmp (protocol, "dvd")) {
    insanity_test_validate_step (test, "uri-is-dvd", FALSE, NULL);
    g_value_unset (&uri);
    return FALSE;
  }
  insanity_test_validate_step (test, "uri-is-dvd", TRUE, NULL);

  global_state = 0;
  global_next_state = 0;
  global_random_command_counter = 0;
  global_waiting_on_playing = TRUE;

  g_object_set (global_pipeline, "uri", g_value_get_string (&uri), NULL);
  g_value_unset (&uri);

  if (gst_element_set_state (global_pipeline, GST_STATE_PLAYING) == GST_STATE_CHANGE_FAILURE) {
    return FALSE;
  }
  if (gst_element_get_state (global_pipeline, NULL, NULL, GST_SECOND) == GST_STATE_CHANGE_FAILURE) {
    return FALSE;
  }

  return TRUE;
}

int
main (int argc, char **argv)
{
  InsanityGstPipelineTest *ptest;
  InsanityTest *test;
  gboolean ret;
  GValue vdef = {0};

  g_type_init ();

  ptest = insanity_gst_pipeline_test_new ("dvd-test", "Tests DVD specific features", NULL);
  test = INSANITY_TEST (ptest);

  g_value_init (&vdef, G_TYPE_STRING);
  g_value_set_string (&vdef, "");
  insanity_test_add_argument (test, "uri", "The ISO to test on (dvd:///path/to/iso)", NULL, FALSE, &vdef);
  g_value_unset (&vdef);

  g_value_init (&vdef, G_TYPE_UINT);
  g_value_set_uint (&vdef, 0);
  insanity_test_add_argument (test, "seed", "A random seed to generate random commands", "0 means a randomly chosen seed; the seed will be saved as extra-info", TRUE, &vdef);
  g_value_unset (&vdef);


  insanity_test_add_checklist_item (test, "uri-is-dvd", "The URI is a DVD specific URI", NULL);
  insanity_test_add_checklist_item (test, "select-root-menu", "Root menu selection succeded", NULL);
  insanity_test_add_checklist_item (test, "select-first-menu", "First menu selection succeded", NULL);
  insanity_test_add_checklist_item (test, "retrieve-angles", "The DVD gave a list of supported angles", NULL);
  insanity_test_add_checklist_item (test, "retrieve-commands", "The DVD gave a list of supported commands", NULL);
  insanity_test_add_checklist_item (test, "cycle-angles", "Cycle through each angle of the selected title in turn", NULL);
  insanity_test_add_checklist_item (test, "cycle-unused-commands", "Cycle through a list of unused commands, which should have no effect", NULL);
  insanity_test_add_checklist_item (test, "send-random-commands", "Send random valid commands, going through menus at random", NULL);

  insanity_test_add_extra_info (test, "seed", "The seed used to generate random commands");
  insanity_test_add_extra_info (test, "angles", "Angle information for the selected title");
  insanity_test_add_extra_info (test, "commands", "Available commands information for the current title");

  insanity_gst_pipeline_test_set_create_pipeline_function (ptest,
      &dvd_test_create_pipeline, NULL, NULL);
  insanity_gst_pipeline_test_set_initial_state (ptest, GST_STATE_READY);
  g_signal_connect_after (test, "setup", G_CALLBACK (&dvd_test_setup), 0);
  g_signal_connect_after (test, "bus-message", G_CALLBACK (&dvd_test_bus_message), 0);
  g_signal_connect_after (test, "start", G_CALLBACK (&dvd_test_start), 0);
  g_signal_connect_after (test, "teardown", G_CALLBACK (&dvd_test_teardown), 0);

  ret = insanity_test_run (test, &argc, &argv);

  g_object_unref (test);

  return ret ? 0 : 1;
}
