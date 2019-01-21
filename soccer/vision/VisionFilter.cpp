#include "VisionFilter.hpp"

#include <iostream>

#include <Constants.hpp>
#include <Robot.hpp>

#include "vision/util/VisionFilterConfig.hpp"

VisionFilter::VisionFilter() {
    threadEnd.store(false, std::memory_order::memory_order_seq_cst);
    ballWriter.open("test.csv");

    // Have to be careful so the entire initialization list
    // is created before the thread starts
    worker = std::thread(&VisionFilter::workerThread, this);
}

VisionFilter::~VisionFilter() {
    // Signal end of thread
    threadEnd.store(true, std::memory_order::memory_order_seq_cst);

    // Wait for it to die
    worker.join();

    ballWriter.close();
}

void VisionFilter::addFrames(const std::vector<CameraFrame>& frames) {
    std::lock_guard<std::mutex> lock(frameLock);
    frameBuffer.insert(frameBuffer.end(), frames.begin(), frames.end());
}

void VisionFilter::fillBallState(SystemState* state) {
    std::lock_guard<std::mutex> lock(worldLock);
    WorldBall wb = world.getWorldBall();

    if (wb.getIsValid()) {
        state->ball.valid = true;
        state->ball.pos = wb.getPos();
        state->ball.vel = wb.getVel();
        state->ball.time = wb.getTime();
        ballWriter << wb.getPos().x() << "," << wb.getPos().x() << ","
                   << wb.getVel().x() << "," << wb.getVel().y() << ","
                   << RJ::timestamp(RJ::now()) << std::endl;
    } else {
        state->ball.valid = false;
    }
}

void VisionFilter::fillRobotState(SystemState* state, bool usBlue) {
    std::lock_guard<std::mutex> lock(worldLock);
    std::vector<WorldRobot> yellowTeam = world.getRobotsYellow();
    std::vector<WorldRobot> blueTeam = world.getRobotsBlue();

    // Fill our robots
    for (int i = 0; i < Num_Shells; i++) {
        OurRobot* robot = state->self.at(i);
        WorldRobot wr;

        if (usBlue) {
            wr = blueTeam.at(i);
        } else {
            wr = yellowTeam.at(i);
        }

        robot->visible = wr.getIsValid();
        robot->velValid = wr.getIsValid();

        if (wr.getIsValid()) {
            robot->pos = wr.getPos();
            robot->vel = wr.getVel();
            robot->angle = wr.getTheta();
            robot->angleVel = wr.getOmega();
            robot->time = wr.getTime();
        }
    }

    // Fill opp robots
    for (int i = 0; i < Num_Shells; i++) {
        OpponentRobot* robot = state->opp.at(i);
        WorldRobot wr;

        if (usBlue) {
            wr = yellowTeam.at(i);
        } else {
            wr = blueTeam.at(i);
        }

        robot->visible = wr.getIsValid();
        robot->velValid = wr.getIsValid();

        if (wr.getIsValid()) {
            robot->pos = wr.getPos();
            robot->vel = wr.getVel();
            robot->angle = wr.getTheta();
            robot->angleVel = wr.getOmega();
            robot->time = wr.getTime();
        }
    }
}

void VisionFilter::workerThread() {
    while (true) {
        RJ::Time start = RJ::now();

        {
            // Do update with whatever is in frame buffer
            std::lock_guard<std::mutex> lock1(frameLock);
            {
                std::lock_guard<std::mutex> lock2(worldLock);

                if (frameBuffer.size() > 0) {
                    world.updateWithCameraFrame(RJ::now(), frameBuffer);
                    frameBuffer.clear();
                } else {
                    world.updateWithoutCameraFrame(RJ::now());
                }
            }
        }

        // Wait for the correct loop timings
        RJ::Seconds diff = RJ::now() - start;
        RJ::Seconds sleepLeft = RJ::Seconds(*VisionFilterConfig::vision_loop_dt) - diff;

        if (diff > RJ::Seconds(0)) {
            std::this_thread::sleep_for(sleepLeft);
        } else {
            std::cout << "WARNING : Filter is not running fast enough" << std::endl;
        }

        // Make sure we shouldn't stop
        if (threadEnd.load(std::memory_order::memory_order_seq_cst)) {
            break;
        }
    }
}