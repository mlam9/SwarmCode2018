#include "PickUpController.h"
#include <limits> // For numeric limits
#include <cmath> // For hypot

PickUpController::PickUpController() {
  lockTarget = false;
  timeOut = false;
  nTargetsSeen = 0;
  blockYawError = 0;
  blockDistance = 0;

  targetFound = false;

  result.type = precisionDriving;
  result.pd.cmdVel = 0;
  result.pd.cmdAngularError= 0;
  result.fingerAngle = -1;
  result.wristAngle = -1;
  result.PIDMode = SLOW_PID;
}

PickUpController::~PickUpController() {
}

void PickUpController::SetTagData(vector<TagPoint> tags) {

  if (tags.size() > 0) {

    nTargetsSeen = tags.size();

    double closest = std::numeric_limits<double>::max();
    int target  = 0;
    for (int i = 0; i < tags.size(); i++) { //this loop selects the closest visible block to makes goals for it

      if (tags[i].id == 0) {

        targetFound = true;

        double test = hypot(hypot(tags[i].x, tags[i].y), tags[i].z); //absolute distance to block from camera lens
        if (closest > test)
        {
          target = i;
          closest = test;
        }
      }
      else {
        nTargetsSeen--;

        if(tags[i].id == 256)
        {
          Reset();
          if (has_control)
          {
            cout << "pickup reset return interupt free" << endl;
            release_control = true;
          }
          return;
        }
      }
    }

    float cameraOffsetCorrection = 0.023; //meters;

    blockYawError = atan((tags[target].x + cameraOffsetCorrection)/blockDistance)*1.05; //angle to block from bottom center of chassis on the horizontal.

    ///TODO: Explain the trig going on here- blockDistance is c, 0.195 is b; find a
    blockDistance = hypot(tags[target].z, tags[target].y); //distance from bottom center of chassis ignoring height.

    blockDistanceFromCamera = hypot(hypot(tags[target].x, tags[target].y), tags[target].z);
  }

}


bool PickUpController::SetSonarData(float rangeCenter){

  if (rangeCenter < 0.12 && targetFound) {
    result.type = behavior;
    result.b = nextProcess;
    result.reset = true;
    targetHeld = true;
    return true;
  }

  return false;

}

void PickUpController::ProcessData() {
  if(!targetFound){
    // Do nothing
    return;
  }

  if ( (blockDistance*blockDistance - 0.195*0.195) > 0 )
  {
    blockDistance = sqrt(blockDistance*blockDistance - 0.195*0.195);
  }
  else
  {
    float epsilon = 0.00001; // A small non-zero positive number
    blockDistance = epsilon;
  }

  //if target is close enough
  //diffrence between current time and millisecond time
  long int Tdiff = current_time - millTimer;
  float Td = Tdiff/1e3;

  cout << "distance : " << blockDistanceFromCamera << " time is : " << Td << endl;

  if (blockDistanceFromCamera < 0.15 && Td < 3.9) {

    result.type = behavior;
    result.b = nextProcess;
    result.reset = true;
    targetHeld = true;
  }
  //Lower wrist and open fingures if no locked targt
  else if (!lockTarget)
  {
    //set gripper;
    result.fingerAngle = M_PI_2;
    result.wristAngle = 1.25;
  }
}


bool PickUpController::ShouldInterrupt(){

  ProcessData();

  if (release_control)
  {
    release_control = false;
    has_control = false;
    return true;
  }

  if ((targetFound && !interupted) || targetHeld) {
    interupted = true;
    has_control = false;
    return true;
  }
  else if (!targetFound && interupted) {
    interupted = false;
    has_control = false;
    return true;
  }
  else {
    return false;
  }
}

Result PickUpController::DoWork() {

  has_control = true;

  if (!targetHeld) {
    //threshold distance to be from the target block before attempting pickup
    float targetDistance = 0.15; //meters


    // -----------------------------------------------------------
    // millisecond time = current time if not in a counting state
    //     when timeOut is true, we are counting towards a time out
    //     when timeOut is false, we are not counting towards a time out
    //
    // In summary, when timeOut is true, the robot is executing a pre-programmed time-based block pickup
    // I routine. <(@.@)/"
    // !!!!! AND/OR !!!!!
    // The robot has started a timer so it doesn't get stuck trying to pick up a cube that doesn't exist.
    //
    // If the robot does not see a block in its camera view within the time out period, the pickup routine
    // is considered to have FAILED.
    //
    // During the pre-programmed pickup routine, a current value of "Td" is used to progress through
    // the routine. "Td" is defined below...
    // -----------------------------------------------------------
    if (!timeOut) millTimer = current_time;

    //diffrence between current time and millisecond time
    long int Tdifference = current_time - millTimer;

    // converts from a millisecond difference to a second difference
    // Td = [T]ime [D]ifference IN SECONDS
    float Td = Tdifference/1e3;

    // If we don't see any blocks or cubes turn towards the location of the last cube we saw.
    // I.E., try to re-aquire the last cube we saw.
    if (nTargetsSeen == 0 && !lockTarget)
    {
      // This if statement causes us to time out if we don't re-aquire a block within the time limit.
      if(!timeOut)
      {
        result.pd.cmdVel = 0.0;
        result.pd.cmdAngularError= 0.0;
        result.wristAngle = 0.8;
        // result.fingerAngle does not need to be set here

        // We are getting ready to start the pre-programmed pickup routine now! Maybe? <(^_^)/"
        // This is a guard against being stuck permanently trying to pick up something that doesn't exist.
        timeOut = true;

        // Rotate towards the block that we are seeing.
        // The error is negated in order to turn in a way that minimizes error.
        result.pd.cmdAngularError = -blockYawError;
      }
      //If in a counting state and has been counting for 1 second.
      else if (Td > 1.0 && Td < 2.5)
      {
        // The rover will reverse straight backwards without turning.
        result.pd.cmdVel = -0.2;
        result.pd.cmdAngularError= 0.0;
      }
    }
    else if (blockDistance > targetDistance && !lockTarget) //if a target is detected but not locked, and not too close.
    {
      // this is a 3-line P controller, where Kp = 0.20
      float vel = blockDistance * 0.20;
      if (vel < 0.1) vel = 0.1;
      if (vel > 0.2) vel = 0.2;

      result.pd.cmdVel = vel;
      result.pd.cmdAngularError = -blockYawError;
      timeOut = false;
      nTargetsSeen = 0;
      return result;
    }
    else if (!lockTarget) //if a target hasn't been locked lock it and enter a counting state while slowly driving forward.
    {
      lockTarget = true;
      result.pd.cmdVel = 0.15;
      result.pd.cmdAngularError= 0.0;
      timeOut = true;
      ignoreCenterSonar = true;
    }
    else if (Td > 2.5) //raise the wrist
    {
      result.pd.cmdVel = -0.10;
      result.pd.cmdAngularError= 0.0;
      result.wristAngle = 0;
    }
    else if (Td > 1.7) //close the fingers and stop driving
    {
      result.pd.cmdVel = -0.1;
      result.pd.cmdAngularError= 0.0;
      result.fingerAngle = 0;
      return result;
    }


    // the magic numbers compared to Td must be in order from greater(top) to smaller(bottom) numbers
    if (Td > 4.2 && timeOut) {
      lockTarget = false;
      ignoreCenterSonar = true;
    }
    else if (Td > 4.0 && timeOut) //if enough time has passed enter a recovery state to re-attempt a pickup
    {
      result.pd.cmdVel = -0.15;
      result.pd.cmdAngularError= 0.0;
      //set gripper to open and down
      result.fingerAngle = M_PI_2;
      result.wristAngle = 0;
    }


    if (Td > 4.8 && timeOut) //if no targets are found after too long a period go back to search pattern
    {
      Reset();
      interupted = true;
      result.pd.cmdVel = 0.0;
      result.pd.cmdAngularError= 0.0;
      ignoreCenterSonar = true;
    }
  }

  return result;
}

bool PickUpController::HasWork() {
  return targetFound;
}

void PickUpController::Reset() {

  result.type = precisionDriving;
  result.PIDMode = SLOW_PID;
  lockTarget = false;
  timeOut = false;
  nTargetsSeen = 0;
  blockYawError = 0;
  blockDistance = 0;

  targetFound = false;
  interupted = false;
  targetHeld = false;

  result.pd.cmdVel = 0;
  result.pd.cmdAngularError= 0;
  result.fingerAngle = -1;
  result.wristAngle = -1;
  result.reset = false;

  ignoreCenterSonar = false;
}

void PickUpController::SetUltraSoundData(bool blockBlock){
  this->blockBlock = blockBlock;
}

void PickUpController::SetCurrentTimeInMilliSecs( long int time )
{
  current_time = time;
}
