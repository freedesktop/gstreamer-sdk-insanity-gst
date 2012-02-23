/* Insanity QA system

       insanitygsttest.h

 Copyright (c) 2012, Collabora Ltd <vincent@collabora.co.uk>

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
/**
 * SECTION:insanitygsttest
 * @short_description: GStreamer Test
 * @see_also: #InsanityTest
 *
 * %TODO.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <gst/gst.h>

#include <insanity-gst/insanitygsttest.h>

G_DEFINE_TYPE (InsanityGstTest, insanity_gst_test,
    INSANITY_TYPE_THREADED_TEST);

struct _InsanityGstTestPrivateData
{
  GstPipeline *pipeline;
};

static void
init_gstreamer ()
{
  int argc = 1;
  char **argv;

  argv = g_malloc (2 * sizeof (char*));
  argv[0] = "foo";
  argv[1] = NULL;
  gst_init (&argc, &argv);
  g_free (argv);
}

static gboolean
insanity_gst_test_setup (InsanityTest *test)
{
  InsanityGstTestPrivateData *priv = INSANITY_GST_TEST (test)->priv;

  if (!INSANITY_TEST_CLASS (insanity_gst_test_parent_class)->setup (test))
    return FALSE;

  printf("insanity_gst_test_setup\n");
  init_gstreamer ();

  priv->pipeline = GST_PIPELINE (gst_pipeline_new ("test-pipeline"));

  return TRUE;
}

static gboolean
insanity_gst_test_start (InsanityTest *test)
{
  if (!INSANITY_TEST_CLASS (insanity_gst_test_parent_class)->start (test))
    return FALSE;

  printf("insanity_gst_test_start\n");

  return TRUE;
}

static void
insanity_gst_test_stop (InsanityTest *test)
{
  printf("insanity_gst_test_stop\n");

  INSANITY_TEST_CLASS (insanity_gst_test_parent_class)->stop (test);
}

static void
insanity_gst_test_teardown (InsanityTest *test)
{
  InsanityGstTestPrivateData *priv = INSANITY_GST_TEST (test)->priv;

  printf("insanity_gst_test_teardown\n");

  if (priv->pipeline)
    gst_object_unref (priv->pipeline);

  gst_deinit ();

  INSANITY_TEST_CLASS (insanity_gst_test_parent_class)->teardown (test);
}

static void
insanity_gst_test_init (InsanityGstTest * test)
{
  InsanityGstTestPrivateData *priv = G_TYPE_INSTANCE_GET_PRIVATE (test,
      INSANITY_TYPE_GST_TEST, InsanityGstTestPrivateData);

  test->priv = priv;
}

static void
insanity_gst_test_class_init (InsanityGstTestClass * klass)
{
  GObjectClass *gobject_class = G_OBJECT_CLASS (klass);
  InsanityTestClass *test_class = INSANITY_TEST_CLASS (klass);

  test_class->setup = &insanity_gst_test_setup;
  test_class->start = &insanity_gst_test_start;
  test_class->stop = &insanity_gst_test_stop;
  test_class->teardown = &insanity_gst_test_teardown;

  g_type_class_add_private (klass, sizeof (InsanityGstTestPrivateData));
}

/**
 * insanity_gst_test_new:
 * @name: the short name of the test.
 * @description: a one line description of the test.
 * @full_description: (allow-none): an optional longer description of the test.
 *
 * This function creates a new GStreamer test with the given properties.
 *
 * Returns: (transfer full): a new #InsanityGstTest instance.
 */
InsanityGstTest *
insanity_gst_test_new (const char *name, const char *description, const char *full_description)
{
  InsanityGstTest *test = g_object_new (insanity_gst_test_get_type (),
      "name", name, "desc", description, NULL);
  if (full_description)
    g_object_set (test, "full-desc", full_description, NULL);
  return test;
}
