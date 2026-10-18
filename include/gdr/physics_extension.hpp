#pragma once
#include "gdr.hpp"

struct PhysicsInput : gdr::Input<"Phys"> {
	float xPosition = 0.f;
    float yPosition = 0.f;
    float rotation = 0.f;
    double xVelocity = 0.0;
    double yVelocity = 0.0;

	PhysicsInput() = default;

	PhysicsInput(uint64_t frame, uint8_t button, bool player2, bool down, float xpos, float ypos, float rot, double xvel, double yvel)
		: Input(frame, button, player2, down), xPosition(xpos), yPosition(ypos), rotation(rot), xVelocity(xvel), yVelocity(yvel) {}

	void parseExtension(binary_reader& reader) override {
		reader >> xPosition >> yPosition >> rotation >> xVelocity >> yVelocity;
	}

	void saveExtension(binary_writer& writer) const override {
		writer << xPosition << yPosition << rotation << xVelocity << yVelocity;
	}
};