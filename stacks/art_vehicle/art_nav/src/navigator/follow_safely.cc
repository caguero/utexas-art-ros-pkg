//
// Navigator safe following distance controller
//
//  Copyright (C) 2007 Austin Robot Technology
//  All Rights Reserved. Licensed Software.
//
//  This is unpublished proprietary source code of Austin Robot
//  Technology, Inc.  The copyright notice above does not evidence any
//  actual or intended publication of such source code.
//
//  PROPRIETARY INFORMATION, PROPERTY OF AUSTIN ROBOT TECHNOLOGY
//
//  $Id$
//
//  Author: Jack O'Quin
//

#include "navigator_internal.h"
#include "Controller.h"
#include "obstacle.h"
#include "follow_safely.h"

#include <art/DARPA_rules.hh>

FollowSafely::FollowSafely(Navigator *navptr, int _verbose):
  Controller(navptr, _verbose) {};

FollowSafely::~FollowSafely() {};

void FollowSafely::configure(ConfigFile* cf, int section)
{
  // How close is close enough for stopping before an obstacle?
  // Should at least include front bumper offset and minimum separation.
  close_stopping_distance = cf->ReadFloat(section, "close_stopping_distance",
					  (DARPA_rules::min_forw_sep_travel
					   + ArtVehicle::front_bumper_px
					   + 7.0));
  ART_MSG(2, "\tclose stopping distance is %.3f meters",
	  close_stopping_distance);

  // maximum, minimum and desired following times (in seconds)
  max_following_time = cf->ReadFloat(section, "max_following_time", 7.0);
  min_following_time = cf->ReadFloat(section, "min_following_time", 3.0);
  desired_following_time =
    cf->ReadFloat(section, "desired_following_time",
		  (max_following_time + min_following_time)/2.0f);
  ART_MSG(2, "\tminimum, desired and maximum following times: "
	  "%.1f, %.1f, %.1f secs",
	  min_following_time, desired_following_time, max_following_time);
}

// Set desired speed for avoiding obstacles within a lane.
//
//  returns:
//	Blocked if lane blocked;
//	Collision if car approaching from ahead in this lane;
//	OK otherwise.
//  exit:
//	sets obstacle->last_obst as a side-effect
//
// We may be following a moving car that could stop at any time or
// already be stopped.  If it stops, stop at a safe distance and set
// lane_blocked.  Higher level controllers will decide if we need to
// pass.  Until told to pass, wait for the obstacle to move.
//
// When stopping, the front bumper must be at least one vehicle length
// (DARPA_rules::min_forw_sep_travel) from the obstacle.
//
// Based on pseudo-code from Dr. Peter Stone.
//
Controller::result_t FollowSafely::control(pilot_command_t &pcmd)
{
  bool was_blocked = navdata->lane_blocked;
  navdata->lane_blocked = false;

  float location = obstacle->closest_ahead_in_plan();
  result_t result = OK;

  if (location >= obstacle->maximum_range())
    {
      // no obstacle that matters, leave pcmd unmodified
      return result;
    }

  float following_time =
    Euclidean::DistanceToTime(location, estimate->vel.px);

  if (verbose >= 2)
    ART_MSG(8, "obstacle is %.3f sec ahead at %.3f m/s",
	    following_time, estimate->vel.px);

  // A 2 sec minimum following time at 10mph will cause us to brake
  // hard when still about two car lengths away (~9m).  One length is
  // the minimum distance allowed by the DARPA rules.
  if ((following_time <= min_following_time)
      || (location <= close_stopping_distance))
    {
      // be safe, request immediate stop
      pcmd.velocity = 0.0;

      if (verbose >= 2)
	ART_MSG(8, "Obstacle avoidance requesting immediate halt");

      // when fully stopped, initiate blocked lane behavior
      // (may already be doing it)
      if (navdata->stopped)
	{
	  navdata->lane_blocked = true;
	  if (!was_blocked)
	    {
	      // flag not already set, new obstacle
	      ART_MSG(1, "New obstacle detected!");
	    }
	  result = Blocked;
	}
    }
  else if (following_time < desired_following_time)
    {
      adjust_speed(pcmd, location); // speed up a bit
    }
  else if ((fabsf(estimate->vel.px) < Epsilon::speed) // stopped?
	   || (following_time > desired_following_time))
    {
      adjust_speed(pcmd, location); // slow down a bit
    }

  // The multiple calls to adjust_speed() above could be tightened
  // up some, but they help make the logic clearer.

  // See if there is anyone coming towards us ahead in this lane.
  if (obstacle->car_approaching())
    {
      ART_MSG(1, "Possible collision ahead!");
      result = Collision;
    }

  trace("follow_safely controller", pcmd, result);
  return result;
};


// private methods

// adjust speed to maintain a safe following distance
void FollowSafely::adjust_speed(pilot_command_t &pcmd, float obs_dist)
{
  // try to adjust speed to achieve desired following time, obeying
  // the speed limit (ignore order->min_speed when following)
  float adjusted_speed = fminf(obs_dist / desired_following_time,
			       order->max_speed);

  // at any rate, do not go faster than the desired speed already set
  pcmd.velocity = fminf(pcmd.velocity, adjusted_speed);
}