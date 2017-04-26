#include "mbed.h"
#include <RawSerial.h>
//#include <math.h>

//******************************************************************
// All variables defined below
//******************************************************************

//communication
DigitalOut myled(LED1);
RawSerial pc(USBTX, USBRX);
RawSerial keyOut(p13, p14);
I2C camera1(p9, p10);


//initial camera data
int IRsensorAddress = 0xB0;
int slaveAddress;
char data_buf[16];
char s;
int i;

//point variables
int point1x = 0;
int point1y = 0;
int point2x = 0;
int point2y = 0;
int point3x = 0;
int point3y = 0;
int point4x = 0;
int point4y = 0;

//sensitivity
//Level 5: p0 = 0x96, p1 = 0xFE, p2 = 0xFE, p3 = 0x05
//highest sensitivity to more accurately detect points
int sen0 = 0x96;
int sen1 = 0xFE;
int sen2 = 0xFE;
int sen3 = 0x00;

//previous point values
int prevX = 1023;
int prevY = 1023;

//matrices of x and y coordinates from the first camera
int onex[4];
int oney[4];

//matrices of x and y coordinates from prev point
int prevx[4];
int prevy[4];

//movement
const int deadzone = 1;
const int mouseMoveMult = 3;
const double mouseMovePwr = 1.4;

//click state
const int CLICK_DEAD_ZONE = 20;
int clickBaseX;
int clickBaseY;
int clickDurCount = 0;
bool readingClick = false;
int minLeftClickDur = 0;
int maxLeftClickDur = 100;



//MOUSE STATE
//implemented for ticker behavior
//ticker depends on these values to update the state/location of the mouse
Ticker mouseStateTicker;
short updatex[4];
short updatey[4];
bool toLeftClick = false;
bool toRightClick = false;



//READING FROM CAMERA VIA INTERRUPT
Ticker cameraReadTicker;


//LED
DigitalOut myled2(LED2);



//******************************************************************
// All methods defined below
//******************************************************************

//takes in values for the movement in the x and y direction 
//also can indicate whether you want to "click"
//NOTE: hard coded wait of 0.1
void mouseCommand(char buttons, short x, short y) {
  
  x = (x > 0) ? pow(mouseMoveMult*x, mouseMovePwr) : -pow(-mouseMoveMult*x, mouseMovePwr);
  //x = x*abs(x);
  //x = x*sqrt((float)abs(x));
  y = (y > 0) ? pow(mouseMoveMult*y, mouseMovePwr) : -pow(-mouseMoveMult*y, mouseMovePwr);
  //y = y*abs(y);
  //y = y*sqrt((float)abs(y));
  
  keyOut.putc(0xFD);
  keyOut.putc(0x00);
  keyOut.putc(0x03);
  keyOut.putc(buttons);
  keyOut.putc(x);
  keyOut.putc(y);
  keyOut.putc(0x00);
  keyOut.putc(0x00);
  keyOut.putc(0x00);
  
  //delay for pushing data
  //wait(0.1); //how large does this need to be?
}




//the interrupt to update mouse state
//run every 100 us
void updateMouseState(){
    
    myled2 = 1 - myled2;
    
    
    //move mouse
    //handles only single finger actions
    mouseCommand(0, updatex[0], updatey[0]);
    
    //clear out changes
    updatex[0] = 0;
    updatey[0] = 0;
    
    //click
   if(toLeftClick){
       //send command to 
       mouseCommand(0x01, 0 , 0);
       
   } else if (toRightClick){
       mouseCommand(0x02, 0 , 0);
   }
    
    //fip clicking to false
    toLeftClick = false;
    toRightClick = false;
    
}


//moves mouse on screen from one finger input
//param
// current point (currx, curry)
// previous point (prevx, prevy)
//TODO: implement additional param to indicate which finger you are looking at
//      current implementation defaults to zero (finger one)
void oneFingerResponse(int currx, int curry, int prevx, int prevy){
    //look at delta btwn prev val and current
    //TODO: moving average
    if((prevx != 1023 || prevy != 1023) && (currx != 1023 && curry != 1023)){
        short diffX = currx - prevx;
        short diffY = -1*(curry - prevy);
    
        //fix diffX
        if(abs(diffX) > 10) {
            diffX = 0;
        } else if(diffX > deadzone){
            diffX -= deadzone;
        } else if (diffX < -1*deadzone){
            diffX += deadzone;
        } else{
            diffX = 0;
        }
        //fix diffY
        if(abs(diffY) > 10) {
            diffY = 0;
        } else if(diffY > deadzone){
            diffY -= deadzone;
        } else if (diffY < -1*deadzone){
            diffY += deadzone;
        } else{
            diffY = 0;
        }
        
        
        //mouseCommand(0, (char) diffX, (char) diffY);
        //TODO: this is defaulting to first finger - need to fix this
        //update target position to move x and y
        updatex[0] = diffX;
        updatey[0] = diffY;
        
       
//        pc.printf("updating x to : %d", diffX);
//        pc.printf("\t updating y to : %d \n", diffY);  
        
//        pc.printf("updating x to : %d", updatex[0]);
//        pc.printf("\t updating y to : %d \n", updatex[0]);
        
    } 
}


//writes two bytes to the camera
void write2bytes(char data1, char data2){
    char out[2];
    out[0] = data1;
    out[1] = data2;
    camera1.write(slaveAddress, out, 2);
    wait(0.01);   
}



// Initialize WiiMote Camera
void initCamera(void){
    write2bytes(0x30, 0x01); 
    write2bytes(0x00, 0x02); 
    write2bytes(0x00, 0x00); 
    write2bytes(0x71, 0x01); 
    write2bytes(0x07, 0x00); 
    write2bytes(sen1, 0x1A);
    write2bytes(sen2, sen3); 
    write2bytes(0x33, 0x03); 
    write2bytes(0x30, 0x08);
    //wait(0.1);

}


//update counts for click 
void updateClickState(int currx, int curry, int prevx, int prevy){
    bool xStable = false;
    bool yStable = false; 
    

    if(currx != 1023 && curry != 1023 && readingClick){
        //finger is on surface and you are reading click

        //test stability

        //check x stability
        if (currx == clickBaseX){
            //no movement in x direction
            xStable = true;
        } else if( abs(currx - clickBaseX) < CLICK_DEAD_ZONE){
            //barely moved in x direction
            xStable = true;
        }

        //check y stability
        if( curry == clickBaseY){
            //no movement in y direction
            yStable = true;
        } else if ( abs(curry - clickBaseY) < CLICK_DEAD_ZONE){
            //barely moved in y direction
            yStable = true;
        }

        //if stable, increment count
        if(xStable && yStable){
            clickDurCount = clickDurCount + 1;
        } else{
            //if not stable, no longer reading click, counter to zero
            readingClick = false;
            clickDurCount = 0; 
        }

        
    } else if (currx != 1023 && curry != 1023 && prevx == 1023 && prevy == 1023 ){
        //finger has been placed on surface

        //set reading click to true
        readingClick = true;

        //save initial location
        clickBaseX = currx;
        clickBaseY = curry;

    } else if (currx == 1023 && curry == 1023 && readingClick){
        //stable click and finger was removed

        //if within bounds, you want to click
        if(clickDurCount > minLeftClickDur &&  clickDurCount < maxLeftClickDur){
            //set state to indicate left click
            toLeftClick = true;
            pc.printf("********LEFT mouse click \n");
        }


        //no longer reading click
        readingClick = false;
        //reset counter
        clickDurCount = 0;
    }
            
}


//get data from camera one 
//populates onex and oney with values depending on the measured points
//NOTE: 1023 means nothing was detected
void readCameraData(void){
        
    //pc.printf("in read camera data \n");
        
    //request data from camera 
    char out[1];
    out[0] = 0x36;   
    camera1.write(slaveAddress, out, 1);
    //wait(0.2); //do we need this?
    
    //get data from camera
    camera1.read(slaveAddress, data_buf, 16);
        
    //POINT 1
    //get data
    point1x = data_buf[1];
    point1y = data_buf[2];
    s = data_buf[3];
    //load x,y    
    onex[0] = point1x + ((s & 0x30) << 4);
    oney[0] = point1y + ((s & 0xC0) << 2);
    
    
    //>>>>>>>>>>>>>>>>>Begin unfinished code for moving 

    oneFingerResponse(onex[0], oney[0], prevX, prevY);    
    updateClickState(onex[0], oney[0], prevX, prevY);
    
    
    //update prev values
    prevX = onex[0];
    prevY = oney[0];
    
    
    //<<<<<<<<<<<<<<<<End unfinished code for moving averages
    
    //>>>>>>>>>>>>>>>>Begin unused parsing for multiple fingers
 //   //POINT 2
//    //get data
//    point2x = data_buf[4];
//    point2y = data_buf[5];
//    s = data_buf[6];
//    //load x,y
//    onex[1] = point2x + ((s & 0x30) << 4);
//    oney[1] = point2y + ((s & 0xC0) << 2);
//      
//    //POINT 3
//    //get data
//    point3x = data_buf[7];
//    point3y = data_buf[8];
//    s = data_buf[9];
//    //load x,y
//    onex[2] = point3x + ((s & 0x30) << 4);
//    oney[2] = point3y + ((s & 0xC0) << 2);
//    
//    //POINT 4
//    //get data
//    point4x = data_buf[10];
//    point4y = data_buf[11];
//    s = data_buf[12];
//    //load x,y
//    onex[3] = point4x + ((s & 0x30) << 4);
//    oney[3] = point4y + ((s & 0xC0) << 2);
    //<<<<<<<<<<<<<<<<<<<<<<End unused parsing for multiple fingers
    
}

//print to serial monitor the coordinates of the points stored in
//the passed x and y arrays
void printCamData(int xcor[4], int ycor[4]){
    for(int i = 0; i<4; i++){
        int x = xcor[i];
        int y = ycor[i];
        //determine what to print
        //x coordinate
        pc.printf(" %d,", x);
        
        //y coordinate
        pc.printf(" %d\t", y);     
    }
    
    //new line and delay
    pc.printf("\n");      
    //wait(0.01);
}




//entrance to the code
int main() {
    
    //i2c increase
    camera1.frequency(400000);
    
    //set values initially to zero
    updatex[0] = 0;
    updatey[0] = 0;
    
    myled = 0;
    myled2 = 0;
    
    //slaveAddress = IRsensorAddress >> 1;
    slaveAddress = IRsensorAddress;
    initCamera();
    
    //update baud rate
    pc.baud(115200);
    
    //attach ticker for interrupt
    //mouseStateTicker.attach_us(&updateMouseState, 100);
    mouseStateTicker.attach(&updateMouseState, 0.05);
    
    //attach ticker for reading camera interrupt
    cameraReadTicker.attach(&readCameraData, 0.01);
    
    
    //loop to search for new info using the camera    
    while(1) {

        //pc.printf("while\n");
        
        //toggle test LED 
        myled = 1 - myled;
        
        //pc.printf("while2\n");
        
        //DEPRECATED: now interrupt
        //get the camera data
        //readCameraData();
        
        //printing clicking state -- FOR DEBUGGING
//        pc.printf("readyForClick %s", readyForClick ? "true" : "false");
//        pc.printf("\treadingClick %s", readingClick ? "true" : "false");        
//        pc.printf("\treadyForClickRelease %s\n", readyForClickRelease ? "true" : "false");        
        
        
        
        
        //printing mouse state -- FOR DEBUGGING
//        pc.printf("update mouse %d, %d", updatex[0], updatey[0]);
//        pc.printf("\tclick left %s", toLeftClick ? "true" : "false");
//        pc.printf("\tclick right %s\n", toRightClick ? "true" : "false");
        
        //print points
        printCamData(onex, oney);  
        
        
        
        
        
        
        //uncomment below to test infinite print
        //keyOut.putc(0x41);
        
        //uncomment below to infinitely move mouse in a square
//        double delay = 0.1;
//        int change = 75;
//        mouseCommand(0,0, (char) -1*change);
//        wait(delay);
//        mouseCommand(0,(char) -1*change,0);
//        wait(delay);  
//        mouseCommand(0,0, (char) change);
//        wait(delay);
//        mouseCommand(0,(char) change,0);
//        wait(delay);  
        
        
    }
}
 