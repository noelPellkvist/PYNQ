#include "stdio.h"
#include "stdint.h"


class Helper{

    private:
    const uint16_t mcp_steps {257};
    uint16_t max_ampl;
    uint16_t min_ampl;
    uint16_t max_offset;
    uint16_t min_offset;
    bool error;

    public:
    Helper(uint16_t *max_ampl, uint16_t *min_ampl, uint16_t *max_offset, uint16_t *min_offset);
    ~Helper() = default;

    uint16_t get_rheo_from_amplitude(uint16_t *amplitude);
    uint16_t get_pot_from_offset(uint16_t *offset);
};

Helper::Helper(uint16_t *max_ampl, uint16_t *min_ampl, uint16_t *max_offset, uint16_t *min_offset){

    if (max_ampl == nullptr || max_offset == nullptr || min_ampl == nullptr || min_offset == nullptr){
        this->error = true;
    } else {
        this->max_ampl = *max_ampl; 
        this->min_ampl = *min_ampl;
        this->max_offset = *max_offset;
        this->min_offset = *min_offset;
    }

}

uint16_t Helper::get_rheo_from_amplitude(uint16_t *amplitude){

    float temp = (this->mcp_steps * (float)*amplitude / (float)this->max_ampl);

    return (uint16_t)temp;

}

uint16_t Helper::get_pot_from_offset(uint16_t *offset){
    
    float temp = (this->mcp_steps * (float)*offset / (float)this->max_offset);

    return (uint16_t)temp;

}

