#pragma once

#include <span>
#include "model/pattern.hpp"

namespace bork{

struct machine {

public:
    enum class MachineType {
        Sampler,
        BlahBlah,
        Num
    };


    machine(MachineType type) : type_(type) {};
	virtual ~machine() = default;

	MachineType type() {return type_;}

	virtual void trigger(const step& event) noexcept = 0;
	virtual void process_chunk(std::span<float> output) noexcept = 0;
	
private:
    MachineType type_;
};

} //namespace bork
