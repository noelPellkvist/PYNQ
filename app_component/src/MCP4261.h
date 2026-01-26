
#include <sys/_stdint.h>
#include <xil_types.h>
extern "C" {
    #include <stdio.h>
    #include "xil_printf.h"
    #include "xparameters.h"
    #include "xgpio.h"
    #include "sleep.h"
    #include <sys/types.h>
    #include <xspi.h>
}

//the HW_GPIO_OFFSET here is hardware specialised, 
//check the port setting in Vivado and change it correspondingly!!!

//also, if users need >1 AXI GPIO in vivado for different MCP chip controlling,
//just add more constant and input when generating MCP instance
#define HW_GPIO_OFFSET_PYNQ 0x4U //for PYNQ-Z1 architecture
//#define HW_GPIO_OFFSET 0x1U //for Andrii's FPGA
#define SPI_CS_AMP 0b01
#define SPI_CS_VCOM 0b10
// Commands
#define CMD_READ        3U
#define CMD_WRITE       0U
#define CMD_INCREMENT   1U
#define CMD_DECREMENT   2U

// Since some commands are 8bit and some are 16bit BUT 
// we use 8bit-wide buffer (2 element array) and the data bits [7:0]
// are always at the same position, the offsets are for 8bit number
#define ADDRESS_BIT_OFFSET      12U
#define COMMAND_BIT_OFFSET      10U
#define MAX_WIPER               256U
#define CMDERR_BIT_MASK         0x100         
#define CMDERR_BIT_MASK_8bit    0x2

// Define the address bits of the MCP register map

#define ADDR_VOLATILE_WIPER_0       0x00U
#define ADDR_VOLATILE_WIPER_1       0x01U
#define ADDR_NON_VOLATILE_WIPER_0   0x02U
#define ADDR_NON_VOLATILE_WIPER_1   0x03U
#define ADDR_VOLATILE_TCON_REG      0x04U
#define ADDR_STATUS_REGISTER        0x05U


#define LSB_MASK        0x00FFU     // mask for the least significant 8 bits
#define MSB_MASK        0xFF00U     // mask for the most significant 8 bits
#define READ_MASK       0x01FFU     // mask for the 9bit read results

//constant for the set/get value parameter
#define VOLATILE true
#define NON_VOLATILE false

//constant for the GPIO wiper_protect, shut_down setting on MCP chips
#define ACTIVATE true
#define INACTIVATE false


typedef enum {nWP_PIN=1, nSHDN_PIN=2, both=3} mcp_gpio_t;

//#define DEBUG 1

class MCP4261{

    private:
        uint8_t deviceID;
        uint8_t rx_buffer[2];
        uint8_t tx_buffer[2];
        uint8_t BUFFER_SIZE;
        uint8_t HW_GPIO_OFFSET;

        bool error;

        uint32_t CS_PIN;

        //helper functions
        bool spi_command_and_transmit(uint8_t address, uint16_t command, uint16_t data);
        bool wiper_validation (uint8_t wiper_id);
        uint8_t address_generation(uint8_t wiper_id, bool is_volatile);

        int spi_status;
        XSpi *spi_instance;
        XGpio *Gpio_instance;


    public:
        bool set_wiper(uint8_t wiper_id, uint16_t value, bool is_volatile);
        bool get_wiper(uint8_t wiper_id, uint16_t &result_ptr, bool is_volatile);
        

        bool set_TCON(uint8_t value);
        bool get_TCON(uint8_t& result_ptr);

        bool get_SR(uint8_t& result_ptr);

        bool step_wiper(uint8_t wiper_id, bool is_up);

        bool get_error_status();
        void clear_buffers();
        bool set_write_protect_shutdown(bool is_active, mcp_gpio_t& pin); 
        bool get_write_protect_shutdown(uint32_t &read_value);

        MCP4261(uint32_t CS_PIN, XSpi *spi_instance, uint8_t BUFFER_SIZE, XGpio *Gpio_instance, UINTPTR BaseAddress, uint8_t HW_GPIO_OFFSET);
        ~MCP4261() = default;
};


/*
Constructor used to initialize basic fields.
The user can check the error bool.
*/
MCP4261::MCP4261(uint32_t CS_PIN, XSpi *spi_instance, uint8_t BUFFER_SIZE, XGpio *Gpio_instance, UINTPTR BaseAddress, uint8_t HW_GPIO_OFFSET) {
    // initialize the fields    
    this->error = false;
    this->CS_PIN = CS_PIN;
    this->HW_GPIO_OFFSET=HW_GPIO_OFFSET;
    
    if(spi_instance == NULL){
        this->error = true;        
    } else {
        this->spi_instance = spi_instance;
    }
    
    this->BUFFER_SIZE = BUFFER_SIZE;

    // initialize the GPIOs for the nWP and nSHDN pins, both active low
    if(Gpio_instance == NULL || BaseAddress == NULL){
        this->error = true;
    } else {
        this->Gpio_instance = Gpio_instance;
        XGpio_Initialize(Gpio_instance, BaseAddress);
        XGpio_SetDataDirection(Gpio_instance, 1, 0x0);
        XGpio_DiscreteWrite(Gpio_instance, 1, 0x03<<HW_GPIO_OFFSET);    // set both pins to 1
    }
}

bool MCP4261::get_write_protect_shutdown(uint32_t &read_value){
    if(this->Gpio_instance == NULL){
        printf("ERROR: GPIO pointer is NULL \n");
        return false;
    }        

    read_value = XGpio_DiscreteRead(this->Gpio_instance, 1);
    return true;
}

/*
sets the nWP or nSHDN to 0 if is_active==true, otherwise (inactivate) sets it to 1 
*/
bool MCP4261::set_write_protect_shutdown(bool is_active, mcp_gpio_t &pins){

    if(this->Gpio_instance == nullptr){
        return false;
    }        

    uint32_t mask_value = (pins << HW_GPIO_OFFSET);

    //XGpio_DiscreteClear and Set methods do not affect the other GPIOs than those specified
    if(is_active){  // set the given GPIO to low -> since it is active low 
        XGpio_DiscreteClear(this->Gpio_instance, 1, mask_value);
    } else { // set the given GPIO to high -> since it is active low 
        XGpio_DiscreteSet(this->Gpio_instance, 1, mask_value);
    }
    
    return true;
}

//helper function: check wiper is either 0 or 1
bool MCP4261::wiper_validation(uint8_t wiper_id){
    if((wiper_id != 0) && (wiper_id != 1)){
        printf("Error: incorrect wiper id. Expect 0 or 1, but got %d \n", wiper_id);
        return false;
    } else return true;
}

//helper function: choose correct address
uint8_t MCP4261::address_generation(uint8_t wiper_id, bool is_volatile){
    uint8_t temp_addr;
    if(wiper_id == 0) {
        temp_addr = is_volatile ? ADDR_VOLATILE_WIPER_0 : ADDR_NON_VOLATILE_WIPER_0;
    } else if (wiper_id == 1){
        temp_addr = is_volatile ? ADDR_VOLATILE_WIPER_1 : ADDR_NON_VOLATILE_WIPER_1;
    }
    return temp_addr;
}

/*
Write to volatile register 0 to set the resistance.
Params: wiper_id - 0 or 1 corresponding to a wiper
        *value - pointer to value to be set, should be less than 
*/
bool MCP4261::set_wiper(uint8_t wiper_id, uint16_t value, bool is_volatile){

    uint8_t temp_addr;
    bool command_result {false};

    // If a wrong wiper_id is passed, print to report FAIL and return failure
    if (!wiper_validation(wiper_id)) return false;

    temp_addr=address_generation(wiper_id, is_volatile);
    
    command_result = spi_command_and_transmit(temp_addr, CMD_WRITE, value);

    return command_result;
}

/*
Read to volatile register 0 or 1 to Read the resistance.
Params: wiper_id - 0 or 1 corresponding to a wiper

NB: read NV memeory takes much time!!!!
please add usleep function within different NV read function
usleep(100) isn't enough, how much time to add needs to be detected(might related to hardware)
*/


bool MCP4261::get_wiper(uint8_t wiper_id, uint16_t &result, bool is_volatile){

    uint16_t read_const=0;
    uint16_t temp_read_results {0};
    uint8_t temp_addr;
    bool command_result {false};
    uint8_t upper_mask=0x1;//to catch LSB of Rx_buffer[0]

    // If a wrong wiper_id is passed, print to report FAIL and return failure
    if (!wiper_validation(wiper_id)) return false;

    temp_addr=address_generation(wiper_id, is_volatile);

    command_result = spi_command_and_transmit(temp_addr, CMD_READ, read_const);
     
    // check the result is valid and write back to the pointer
    if(command_result){
        temp_read_results = (((rx_buffer[0] & upper_mask)<<8) | rx_buffer[1]);
        if(temp_read_results<256) result=temp_read_results;
        else {
            print("invalid read result [>256] \n");
            return false;}
        return true;
    } 
    else return false;
}


/*
Increments the wiper.
Parameters: wiper_id can be 0x0 or 0x1
*/

bool MCP4261::step_wiper(uint8_t wiper_id, bool is_up){

    // wiper 0 increment 0000 0100
    // wiper 1 increment 0001 0100
    // wiper 0 decrement 0000 1000
    // wiper 1 decrement 0001 1000

    // If a wrong wiper_id is passed, print to report FAIL and return failure
    if(!wiper_validation(wiper_id)) return false;


    // clear tx and rx buffers
    this->clear_buffers();
    uint8_t temp_command;
    
    // is_up means incrementing
    temp_command = is_up ? 0b00000100 : 0b00001000;
    this->tx_buffer[0] = (wiper_id << 4) | temp_command;

    if (this->spi_instance == nullptr){
        return false;
    }

    XSpi_SetSlaveSelect(this->spi_instance, this->CS_PIN);
    
    this->spi_status = XSpi_Transfer(this->spi_instance, this->tx_buffer, this->rx_buffer, 1);

    if(this->spi_status != XST_SUCCESS){
        return false;
    }

    // Checking the CMDERR bit from the slave device
    if((this->rx_buffer[0] & 0b00000010) != 0x2) {
        return false;
    }    

    return true;

}

/*
Returns the error status
*/
bool MCP4261::get_error_status(){
    return this->error;
}

void MCP4261::clear_buffers(){
    this->rx_buffer[0] = 0;
    this->rx_buffer[1] = 0;
    this->tx_buffer[0] = 0;
    this->tx_buffer[1] = 0;
}

bool MCP4261::set_TCON(uint8_t value){
    bool command_result {false};
    int16_t int16_value {0};
    int16_value = (0x00ff) & (value);
    
    command_result = spi_command_and_transmit(ADDR_VOLATILE_TCON_REG, CMD_WRITE, int16_value);
    return command_result;
}

bool MCP4261::get_TCON(uint8_t& result){
    bool command_result {false};
    uint16_t read_const=0;
    uint16_t temp_read_results {0};
    command_result = spi_command_and_transmit(ADDR_VOLATILE_TCON_REG, CMD_READ, read_const);
    
    if(command_result){
        #ifdef DEBUG
        print("in TCON reading \n");
        printf("DEBUG: tx_buffer[0] %x\n", tx_buffer[0]);
        printf("DEBUG: tx_buffer[1] %x\n", tx_buffer[1]);        
        printf("DEBUG: rx_buffer[0] %x\n", rx_buffer[0]);
        printf("DEBUG: rx_buffer[1] %x\n", rx_buffer[1]);
    #endif
        temp_read_results = rx_buffer[1];
        result=temp_read_results;
        return true;
    } else return false;
}

bool MCP4261::get_SR(uint8_t& result){
    bool command_result {false};
    uint16_t read_const=0;
    uint16_t temp_read_results {0};
    command_result = spi_command_and_transmit(ADDR_STATUS_REGISTER, CMD_READ, read_const);
    
    if(command_result){
        #ifdef DEBUG
        print("in SR reading \n");
        printf("DEBUG: tx_buffer[0] %x\n", tx_buffer[0]);
        printf("DEBUG: tx_buffer[1] %x\n", tx_buffer[1]);        
        printf("DEBUG: rx_buffer[0] %x\n", rx_buffer[0]);
        printf("DEBUG: rx_buffer[1] %x\n", rx_buffer[1]);
        #endif

        temp_read_results = rx_buffer[1];
        result=temp_read_results;
        return true;
    } else return false;
}


/*
Private method to create a 16 bit value for further SPI transfer
Params: 4bit address, read_write=true if read; data
cc=0b11 if read; 0b00 if write

spi_string will carry the corret 16bit SPI command after the function

*/
//bool MCP4261::spi_command_and_transmit(uint8_t address, uint16_t command, uint16_t *data){
bool MCP4261::spi_command_and_transmit(uint8_t address, uint16_t command, uint16_t data){

    uint16_t spi_string {0};
    spi_string |= (address << ADDRESS_BIT_OFFSET) | (command << COMMAND_BIT_OFFSET);


    // check the range of the data line
    if (data <= MAX_WIPER) { // 0x100, 0 <= value <= 256
        spi_string |= data;
    } else {
        return false;
    }

    #ifdef DEBUG
        printf("DEBUG: spi_string %x\n",spi_string);
    #endif

    // clear tx and rx buffers
    this->clear_buffers();
    XSpi_SetSlaveSelect(this->spi_instance, this->CS_PIN);

    // the transfer is MSB first -> the first byte goes to 0th array element
    this->tx_buffer[0] = (uint8_t)((spi_string & MSB_MASK) >> 8);
    this->tx_buffer[1] = (uint8_t)(spi_string & LSB_MASK);
    
     
    #ifdef DEBUG
        printf("DEBUG: tx_buffer[0] %x\n", tx_buffer[0]);
        printf("DEBUG: tx_buffer[1] %x\n", tx_buffer[1]);        
        printf("DEBUG: rx_buffer[0] %x\n", rx_buffer[0]);
        printf("DEBUG: rx_buffer[1] %x\n", rx_buffer[1]);
    #endif

    if(this->spi_instance == nullptr){
        print("ERROR: No SPI instance given before transmitting\n");
        return false;
    }   
    //do the SPI transfer, the tx_buffer is the command, and rx_buffer recieve the information
    this->spi_status = XSpi_Transfer(this->spi_instance, this->tx_buffer, this->rx_buffer, 2);

    if(this->spi_status != XST_SUCCESS){
        printf("error at XSpi_Transfer\n");
        return false;
    }

    // Checking the CMDERR bit from the slave device
    // The CMDERR bit = 1 if the combination of address and command is invalid
    if((this->rx_buffer[0] & 0b00000010) != 0x2) {
        printf("CMDERR\n");
        return false;
    }

    return true;
}
