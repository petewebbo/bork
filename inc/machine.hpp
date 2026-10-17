#pragma once

#include <span>
#include <pattern.hpp>

namespace bork{

struct machine {

public:
    enum class MachineType {
        Sampler,
        BlahBlah,
        Num
    };


    machine(MachineType type) : _type(type) {};
	virtual ~machine() = default;

	MachineType type() {return _type;}

	virtual void trigger(const step& event) noexcept = 0;
	virtual void process_chunk(std::span<float> output) noexcept = 0;
	
private:
    MachineType _type;
};

} //namespace bork
