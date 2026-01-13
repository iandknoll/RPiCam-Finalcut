// FUTURE NOTE: I wrote this code in part as a learning experience for C++
// I did my best to follow repostory implementations, bug test, and error handle
// Nevertheless, there are likely to be shortcomings and issues not fully addressed
// As a matter of fact, I *know* both exist, though I believe them to be minor
// If you are maintaining this, I grant full permission to mock my feeble efforts

// CAMERA DEPENDENCIES
#include "core/rpicam_encoder.hpp" 	// Contains code for encoding video w/ rpicam
#include "core/logging.hpp"			// Contains code for the logging macro used
#include "output/output.hpp"  		// Contains code for outputing video to file
#include <functional>				// Needed to use std::bind
#include <memory>					// Needed for smart pointers

// FRONT END DEPENDENCIES
#include <final/final.h> 			// Includes the basic FinalCut library
#include <ctime> 					// Need for current date/time
#include <chrono>					// Need for stopwatch
#include <filesystem> 				// Need to check if file(s) exist

// SHARED DEPENDENCIES
#include <string>					// Required for std::string type
#include <atomic>					// Required for our global variable
#include <thread>					// Required for threading
#include <mutex>					// Required for thread syncronization
#include <stdexcept>				// Required for throwing standard exceptions


// GLOBALS
enum class StopType {
	USER = 0,
	TIMEOUT = 1,
	ERROR = -1
};
// enum: user defined data type assigning names to integer values
// mainly for readability-- using here to have plaintext names for function returns

struct CameraStopInfo {
	StopType type = StopType::ERROR;	// why did camera stop?
	std::string error_message = "";		// error messages (if applicable)
};
// struct: user defined class for storing data of multiple types in one variable
// a struct differs from a standard class in that members are public by default)
// This means that you can directly access them from outside of the struct

static std::atomic<bool> stop_camera{false};		// flag for camera stop requests
static std::atomic<bool> camera_finished{false};	// flag to indicate camera stops
// In this context, "static" means variable is allocated once, for lifetime of program
// We will run our camera on a seperate thread to the TUI (so they can operate parallel)
// Because of this, anything both can access needs to be set as atomic--
// In brief, this prevents other threads from seeing mid-write (garbage) states.
// This is what we're doing here with the "std::atomic<bool>" part

static std::mutex camera_stop_info_mutex;
// declare an object of type std::mutex, to aid in thread synchronization
// A mutex (short for mutual exclusion) is similar to a lock--
// To access the resources/code protected by it, a thread must lock the mutex
// In doing so, it prevents others threads from accessing until you unlock

static CameraStopInfo camera_stop_info;
// declare a CameraStopInfo struct, filling with default placeholder values

// CAMERA
static int get_colourspace_flags(std::string const& codec) {
	// Function for handling encoder colour space
	// Copied from rpicam_vid.cpp-- it being static there means we can't reference it
	if (codec == "mjpeg" || codec == "yuv420") {
		// if codec uses a jpeg colorspace...
		return RPiCamEncoder::FLAG_VIDEO_JPEG_COLOURSPACE; // return with such
	}
	else {
		return RPiCamEncoder::FLAG_VIDEO_NONE; // otherwise, nothing needed(?)
	}
	// Side-Note: In C++, one-line if statements do NOT require {}
	// For consistency, safety, and familarity, I always include them
}

void VidStart(std::string const& name) {
	// Define function for running rpicam-vid

	// (Side-Note: :: is a "scope resolution operator". If x::y then:)
	// (x defines a scope-- a region of code where a variable/identifier is accessible)
	// (Many Scopes exist, including Global or Local-- e.g. between any given set of {} )
	// (Here, the scope is a Namespace-- which libaries use to avoid variable conflict)
	// (y then defines an identifer-- a type, function, variable, etc. in said scope)
	// (x::y then just lets us access identifier x in scope y)	

	bool EncoderOn{false};
	bool CameraOn{false};
	// Setup some variables to track Camera/Encoder status for exception handling

	// (Side-Note: The use of {} in the above is known as braced initilization)
	// (As opposed to more traditional assignment initializion (e.g. int x = 1))
	// (Generally, this is the prefered initilization method in C++)
	// (The main reason is it prevents unintended narrowing conversion)
	// (Narrowing conversion results when going from a wider type to a narrower type)
	// (For example, trying to assign the float 3.5 to an int data type)
	// (w/ assignment initilization, the result is 3 (a loss in data!))
	// (w/ braced initilization, you would get a compilation error for trying to do this)
	// (I will note that I do not use braced initilization in ever possible spot here--)
	// (It's easy to miss-- old habits die hard-- and hampers readibility at times imo)
	// (You also *don't* use it when reassigning an already declared variable)
	// (In that context, stick to traditional assignment, or pay the price-- errors!)

	RPiCamEncoder app;
	// Creates an object of the class "RPiCamEncoder" with name "app"
	// RPiCamEncoder handles the video recording pipeline
	// We do this outside of the try block so it's in scope for catch

	auto TryEncoderOff = [&app, &EncoderOn]() {
		try {
			if (EncoderOn) {
				app.StopEncoder();
				EncoderOn = false;		// technically not needed, but safe practice
			}
		} catch (std::exception const &e) {
			LOG_ERROR("ERROR: Unable to stop encoder: " << e.what());
			// Use LOG_ERROR() macro to send error to log/terminal (depending on how code is run)
			// e.what() is a method for type std::exception to outputs error as a string pointer

			// (Side-Note: A macro is a placeholder for preprocessor to replace before compilation)
			// (A macro can be an object/variable, function (as here), or conditional)
			// (It speed up development by reusing code w/o function calls or explicit rewrites)
			
			throw std::runtime_error("Failed to stop encoder: " +std::string(e.what()));
			// throw an error to trigger the catch of whatever larger function this is included in
		}
	};

	auto TryCameraOff = [&app, &CameraOn]() {
		try {
			if (CameraOn) {
				app.StopCamera();
				CameraOn = false;	//technically not needed, but safe practice
			}
		} catch (std::exception const &e) {
			LOG_ERROR("ERROR: Unable to stop camera:"  << e.what());
			
			throw std::runtime_error("Failed to stop camera: " +std::string(e.what()));
			// throw an error to trigger the catch of whatever larger function this is included in
		}
	};
	// The above are lambda functions (synonymous with anonymous function*)
	// lambda functions use format []() {}:
	// [] contains any variables the function will need in its operations
	// () describes the input argument types/names (like a regular function)
	// {} contains the actual code the lambda function will run
	// They're a quick way of defining behaviour that will be repeated
	// In this case, our protocol for stopping the camera and encoder (see later)

	// Side-Note: Need to specify variables to be capture by reference (e.g. &var)
	// Otherwise, lambda functions will capture by value (create a copy at initilization)
	// Said copy wouldn't reflect updates to the variables-- capture by reference will!

	try
	{
		// try: attempt to run the following code.
		// If an error is detected, run the code under catch:

		VideoOptions *options = app.GetOptions();
		// Creates a pointer to an object of the class "VideoOptions"
		// A pointer stores the memory location of an object (rather than object itself)
		// app.GetOptions() is a function tied to the "RPiCamEncoder"
		// It outputs the address of the object's (in this case "app") VideoOptions
		// VideoOptions is a class of type struct (basically a container to group variables)
		// Naturally, it contains the options we want to pass to the encoder

		// (Side-Note 1: All standard pointers in 64bit systems are 8 byte, unsigned integers)
		// (That said you need to specify the type of the underlying object when definining)
		// (The reason is we need to know how much data to read when dereferencing the pointer)

		// Adjust these values according to your own needs:
		options->output = name; 				// file name (here our input)
		options->timeout.set("40min"); 			// MAX Recording time
		options->codec  = "h264";				// codec for video encoding/decoding
		options->profile = "baseline";  		// Compression profile
		options->framerate = 240;				// fps
		options->width = 800;					// frame width (in pixels)
		options->height = 800;					// frame height (in pixels)
		options->awbgains = "2,2";				// disable automatic white balance
		options->shutter.set("3000us");			// Shutter speed (us)
		options->gain = 2;						// analog gain
		options->denoise = "cdn_off"; 			// turns off color denoise (for fps)
		options->nopreview = true;				// turns off preview (for fps)
		// the arrow operator -> dereferences a pointer (getting the underlying object),
		// then accesses the specified member of the class on the right hand side of arrow
		// here "member" just means a variable, function, etc. defined inside a class/struct

		// Side-Note: Just to drill it in, when trying to access a class method:
		// "->" is for pointers (like "options" here), while "." is for objects


		std::unique_ptr<Output> output = std::unique_ptr<Output>(Output::Create(options));
		// std::unique_ptr is a type of pointer that memory manages it's pointed object
		// specifically, it will delete the object if the pointer is deleted or reassigned
		// the name in <> is the type/class of object being pointed to (here "Output")
		// the following variable (here "output") is in turn the pointer.
		// On left, std::unique_ptr<Output> defines a variable of that type (think "int x")
		// While on the right, the same text constructs an object of said type
		// On the right, the text in () defines how said variable is getting constructed
		// Output::Create(options) creates a pointer to an Output object based on "options"
		// Output can be a few different types, hence the need for a method to decide which
		// Here it's most likely a "FileOutput" type (for writing to file)

		// (Side-note 1: *Technically* a smart pointer is not a pointer itself)
		// (Rather, it's an object that wraps a pointer and offers unique behaviour on top)
		// (*However*, syntactically they're still able to operate as a pointer!)
		// (They accomplish this by overloading operators like -> and *)

		app.SetEncodeOutputReadyCallback(std::bind(&Output::OutputReady, output.get(), 
			std::placeholders::_1, 
			std::placeholders::_2, 
			std::placeholders::_3, 
			std::placeholders::_4));
		// Registers callback function tied to "RPiCamEncoder" class, to handle encoded video
		// A callback function is one that can be passed as argument to another function
		// This one routes newly encoded frames from the encoder to an output handler
		// The function expects four parameters:
			// A pointer to the encoded data buffer
			// The size of the encoded data (in bytes)
			// The timestamp of the frame (as a 64-bit signed integer)
			// And a flag indicating if said frame is a keyframe
		// To give these parameters, we use std::bind, generating a "forward call wrapper"
		// Put simply, std::bind creates a version of a function with some parameters filled
		// Here, function is &Output::OutputReady, and output.get() is pre-defined input.
		// "_1, _2, etc." is placeholder for the first four arguments passed to the callback
		// That way, the # of arguments our wrapped function and callback expect are equal.
		// OutputReady handles the encoded data, doing things like managing output state,
		// saving timestamps/metadata (optional), & calling another function, outputBuffer(),
		// to actually write the encoded frame to its destination (in our case, a file).
		// Since "output" is our smart pointer, output.get() returns its raw pointer.

		// So one last time, in plain English. this line of code is saying:
		// "When an encoded frame is ready, respond by processing it with OutputReady,
		// using output.get()-- which tells it which method to use to route/write data--
		// and the four aformentioned frame parameters as inputs."

		// Side-Note: A wrapper is a class/function that "encapsulates" another class/function
		// It can provide alternative interfaces, added functionality, or resource management

		app.SetMetadataReadyCallback(std::bind(&Output::MetadataReady, output.get(), 
			std::placeholders::_1));
		// Roughly the same idea as the prior line of code, only handling metadata--
		// things like exposure, gain values, white balance, etc.
		// Here the only expected paramater is an object of class libcamera::ControlList,
		// Which contains the metadata for the frame.


		app.OpenCamera();
		// Lots of heavy lifting being done by this one line in the background!
		// For our sake, you just need to know that this selects our desired camera by index
		// (here defaulting to 0) and sets it up to be exclusively handled by this script

		app.ConfigureVideo(get_colourspace_flags(options->codec));
		// Configures our video stream, telling our codec how to process the raw sensor data
		// based on the colourspace inherent to the type of codec we are using.
		// (More configuration going on in the background as well-- configuring buffer count,
		// setting resolution, configuring denoise, etc.-- the whole video pipeline!)

		app.StartEncoder();
		// Starts and configures our encoder, which asynchronously processes video frames
		// while our video stream continues collection. As usual, lot going on in the
		// background here (like setting up encoder callbacks)
		EncoderOn = true;
		// Encoder started, so set flag accordingly

		app.StartCamera();
		// Begins camera hardware (w/ our configured controls), and start capturing frames!
		CameraOn = true;
		// Camera started, so set flag accordingly

		auto start_time = std::chrono::high_resolution_clock::now();
		// Gets the current time (in high resolution) and assigns it to variable start_time
		// auto just tells compiler to pick an appropriate type based on variable's input

		for (;;)  // Create infinite loop by passing no parameters to for loop
		{
			RPiCamEncoder::Msg msg = app.Wait();
			// Wait for camera to delver message, then save to struct "msg" of type Msg
			// Camera can basically send one of the three messages:
				// RequestComplete -- a frame is ready for processing
				// Timeout -- Camera hardware timeout, need to restart camera
				// Quit -- Request to end application/loop

			if (msg.type == RPiCamApp::MsgType::Quit)  // Quit received
			{
				TryCameraOff();			// Try to end camera
				TryEncoderOff();		// Try to end encoder

				{
					// the {} here creates a limited scope--
					// variables declared inside only exist until the end of the block
					
					std::lock_guard<std::mutex> lock(camera_stop_info_mutex);
					// std::lock_guard is a wrapper for handling mutex locking
					// <std::mutex> specifies the behaviour we want, based on mutex type
					// It will run the lock method on camera_stop_info_mutex for us
					// We use it instead of manually locking for its extra functionality--
					// Namely, it automatically unlocks once destroyed / out of scope.
					// Pattern of limited scope w/ mutex lock at start means:
					// If we get here while another thread is in a scope w/ same mutex, 
					// block (wait) that thread finishes its scope and unlocks mutex
					
					camera_stop_info = CameraStopInfo{StopType::USER, ""};
					// All to (safely!) declare new values for camera_stop_info struct
				}
				camera_finished.store(true);	// Let event handler know we're done w/ camera
				// .store() is the method for writing to atomic variable
				
				return;	// End loop early
			}
			else if (msg.type == RPiCamApp::MsgType::Timeout) // Camera timed out
			{
				LOG_ERROR("ERROR: Device timeout detected, attempting restart!");
				// Log the issue in terminal/log file (depending on how program ran)
				try {
					app.StopCamera();
					CameraOn = false;
					app.StartCamera();
					CameraOn = true;
					// Attempt cycling the camera
					// Note: cycling camera like this could *technically* lead to minor data loss
					// That said, it'd be super minor, and timeouts like this are rare
					// This is the exact method rpicam_vid.cpp uses, so its good enough for me!
				}
				catch (std::exception const &e) {
					TryCameraOff();				// Try to end camera
					TryEncoderOff();			// Try to end encoder
					
					{
						std::lock_guard<std::mutex> lock(camera_stop_info_mutex);
						camera_stop_info = CameraStopInfo{
							StopType::ERROR,
							std::string("Camera restart failed: ") + e.what()
						};
						// Modify camera_stop_info in mutex
					}
					camera_finished.store(true);	// Let event handler know we're done w/ camera
					return;							// End loop early
				}
				continue;	// Camera succesfully restarted, continue as normal
			}
			else if (msg.type != RPiCamApp::MsgType::RequestComplete)	// broad error check
			{
				TryCameraOff();				// Try to end camera
				TryEncoderOff();			// Try to end encoder
				
				{
					std::lock_guard<std::mutex> lock(camera_stop_info_mutex);
					camera_stop_info = CameraStopInfo{
						StopType::ERROR,
						"Unexpected message type received"
					};
					// Modify camera_stop_info in mutex
				}
				camera_finished.store(true);	// Let event handler know we're done w/ camera
				return;							// End loop early
			}

			auto now = std::chrono::high_resolution_clock::now();	// Get current time
			if ((now - start_time) > options->timeout.value)	// If elapsed time > timeout
			{
				TryCameraOff();					// Try to end camera
				TryEncoderOff();				// Try to end encoder
				
				{
					std::lock_guard<std::mutex> lock(camera_stop_info_mutex);
					camera_stop_info = CameraStopInfo{
						StopType::TIMEOUT,
						""
					};
					// Modify camera_stop_info in mutex
				}
				camera_finished.store(true);	// Let event handler know we're done w/ camera
				return;							// End loop early
			}

			if (stop_camera.load()) // If the user send a shutdown signal
			{
				//Note: .load() is the how you read an atomic variable
				
				TryCameraOff();				// Try to end camera
				TryEncoderOff();			// Try to end encoder
				
				{
					std::lock_guard<std::mutex> lock(camera_stop_info_mutex);
					camera_stop_info = CameraStopInfo{
						StopType::USER,
						""
					};
					// Modify camera_stop_info in mutex
				}
				camera_finished.store(true);	// Let event handler know we're done w/ camera
				return;							// End loop early
			}
			
			CompletedRequestPtr &completed_request = std::get<CompletedRequestPtr>(msg.payload);
			// Creates a reference variable of type CompletedRequestPtr
			// CompleteRequestPtr is itself a type of smart pointer: a shared_ptr
			// For our purposes, main difference from unique_ptr is you can have multiple
			// shared_ptr responsible for memory managemement of the referenced object.
			// On the right, we define that reference as the contents of msg.payload--
			// That is, the frame data associated with a given message
			// We need std::get<CompletedRequestPtr> as msg.payload is of type "Payload"
			// Which is a type alias of std::variant<CompletedRequestPtr>, itself a "variant"
			// A variant is a "type safe union that can hold one of several types at a time"
			// std::get<CompletedRequestPtr> extracts the variable of type CompleteRequestPtr
			// Put VERY simply: This is the line that gets our actual framedata!

			// (Side-Note: completed_request must be reference due to nature of shared_ptrs)
			// (If it wasn't, both sides of the expression would each create a shared_ptr)
			// (Then, we'd have to worry about both to destory the underlying variable!)
			// (And of course, having to allocate the extra memory would be less efficient)

			app.EncodeBuffer(completed_request, app.VideoStream());
			// Take our frame data (completed_request) and give to app method EncoderBuffer
			// Send it alongside app.VideoStream(), containing encoder settings/methods
  		}
	}
	catch (std::exception const &e)
	{
		// If an error is detected during try, run the following, using "e" as input
		// e is a constant reference, of type std::exception-- standard class for C++ errors
		// Simply put, it's the corresponding error message for whatever failed in try.

		TryCameraOff();			// Try to end camera
		TryEncoderOff();		// Try to end encoder
		
		{
			std::lock_guard<std::mutex> lock(camera_stop_info_mutex);
			camera_stop_info = CameraStopInfo{
				StopType::ERROR,
				std::string("Exception: ") + e.what()
			};
			// Modify camera_stop_info in mutex
		}
		camera_finished.store(true);	// Let event handler know we're done w/ camera
		return;							// If an error occured, end script
	}
}



// FRONT END UI
auto filename_time() -> std::string
{
	// Function to get current date-time as character array

	std::time_t timestamp = std::time(nullptr);
	// Get current time as timestamp object
	
	std::tm datetime;
	// declare a datetime struct (std::tm)
	
	localtime_r(&timestamp, &datetime);
	// Convert our timestamp into std::tm, and assign to datetime
	// & before timestamp and datetime is a address-of operator
	// localtime_r wants a pointer for input-- & gives a pointer for an object

	char output[80];
	// Allocate space for our string

	std::strftime(output, 80, "%m-%d-%y_%H-%M-%S.mp4", &datetime);
	// Write date-time to our char array
	
	return output;
}

class ConfirmButton : public finalcut::FButton
{
	// Defining a new class to handle our main button
	// Colon defines inheretance--
	// "MainButton" class will inherit from "finalcut::FButton" class
	// ie, it will possess all the same members (objects and methods)
	// "public" indicates public members of inherited class remain public

	public:
		// All subsequent members will be public

		explicit ConfirmButton (finalcut::FWidget* parent = nullptr)
			 : finalcut::FButton{parent}
		{
			// Method with same name as class = "constructor"
			// Constructor is always called when an object of the class is made
			// "explicit" is a function specifier for conversion functions
			// It tells compilers not to allow implicit type conversions.
			// This is mostly a safety measure to prevent wrong method use

			// This takes a pointer to a Finalcut Widget as input--
			// Specifically, the widget that serves as our button's parent
			// If no input is given, a default null pointer is used.

			// Finally, we have the method inherit from FButton--
			// Specifically, an instance of FButton w/ pointer "parent" as input

			// Side Note: A "conversion function" converts one type to another.
			// They're declared w/ "operator", or "explicit" for above effect.
			// Any constructor with one input is a conversion function--
			// Since it shows how to convert that input type to our new class.
			
			// Setup function(s) (defined further down):
			initLayout();
		}

	private:
		// All subsequent members will be private

		void initLayout()
		{
			// Defines a function for setup variables
			// "void" tells the compiler the function has no return value
			
			setText ("Start Video");
			setGeometry(finalcut::FPoint{22,8}, finalcut::FSize{12,1});
			// finalcut::FPoint{x,y} handles where the top left corner of a widget goes
			// finalcut::FPoint{w,h} handles the width and height of the widget
			// In both cases, the units are in terms of standard-size text spaces
			// Here FPoint is relative to parent dialog (x,y w,h)			

			finalcut::FButton::initLayout();
			// Run the inheritted class's initLayout (no effect here, but good practice)
		}
};

class YesButton : public finalcut::FButton
{
	// Defining a new class to handle our yes button
	// It will inherit properties from the "finalcut::FButton" class

	public:
		// All subsequent members will be public

		explicit YesButton (finalcut::FWidget* parent = nullptr)
			 : finalcut::FButton{parent}
		{
			// Setup function(s) (defined further down):
			initLayout();
		}

	private:
		// All subsequent members will be private

		void initLayout()
		{
			// Defines a function for setup variables
			
			setText ("Yes");
			setGeometry(finalcut::FPoint{23,8}, finalcut::FSize{3,1});
			// (Here FPoint is relative to parent dialog) (x,y w,h)
			
			finalcut::FButton::initLayout();
			// Run the inheritted class's initLayout (no effect here, but good practice)			
		}
};

class NoButton : public finalcut::FButton
{
	// Defining a new class to handle our no button
	// It will inherit properties from the "finalcut::FButton" class

	public:
		// All subsequent members will be public

		explicit NoButton (finalcut::FWidget* parent = nullptr)
			 : finalcut::FButton{parent}
		{
			// Setup function(s) (defined further down)
			initLayout();
		}

	private:
		// All subsequent members will be private

		void initLayout()
		{
			// Defines a function for setup variables
			setText ("No");
			setGeometry(finalcut::FPoint{32,8}, finalcut::FSize{3,1});
			// (Here FPoint is relative to parent dialog) (x,y w,h)

			finalcut::FButton::initLayout();
			// Run the inheritted class's initLayout (no effect here, but good practice)			
		}
};

class FileName : public finalcut::FLineEdit
{
	// Defining a new class to handle our filename input
	// It will inherit properties from the "finalcut:FlineEdit" class

	public:
		// All subequent members will be public

		explicit FileName (finalcut::FWidget* parent = nullptr)
			 : finalcut::FLineEdit{parent}
		{
			// Setup function(s) (defined further down)
			initLayout();
		}

	private:
		// All subsequent members will be private

		void initLayout()
		{
			// Defines a function for setup variables

			setInputFilter("[a-zA-Z0-9 ._-]");
			// finalcut::FLineEdit has built in functionality to filter inputs:
			// restrict text to alphanumeric, spaces, spaces, dots, hyphens, underscores
			
			setMaxLength(255);
			// set max text length
			
			std::string fsuggestion{filename_time()};
			// Get current date-time as suggested file name

			setText (finalcut::FString{fsuggestion});	// Need to convert type(?)
			setGeometry (finalcut::FPoint{20,4}, finalcut::FSize{30,1});
			// (Here FPoint is relative to parent dialog) (x,y w,h)
			
			setLabelText("File Name: ");
			// Set LabelText (to appear left of input area)

			finalcut::FLineEdit::initLayout();
			// Run the inheritted class's initLayout (no effect here, but good practice)
		}
};

class Stopwatch : public finalcut::FLabel
{
	// Defining a new class to handle our stopwatch (or lack thereof)

	public:
		// All subsequent members will be public

		explicit Stopwatch (finalcut::FWidget* parent = nullptr)
			 : finalcut::FLabel{parent}
		{
			// Setup function(s) (defined further down)
			initLayout();
		}
		
		// Stopwatch functions will need to be public so the main protocol can access:
		void start()
		{
			// Check if stopwatch is off:
			if (!is_running)
			{
				start_time = std::chrono::steady_clock::now();
				// Get our start time
				
				timer_id = addTimer(1000);
				// FinalCut has built in functions for checking timers function
				// This one tells our Stopwatch class to check every 1000ms / 1s.
				
				// Double-check timer was created succesfully:
				if (timer_id <= 0) 	// Valid IDs > 0
				{
					setText("ERROR: Timer failed to start");
				}
				else
				{
					is_running = true;
					// Set running flag on					
				}
			}
		}
		
		void stop()
		{
			// Checks if stopwatch is on
			if (is_running)
			{
				is_running = false;
				// Set running flag off

				// Double-check timer before deleting
				if (timer_id > 0) 	// Valid IDs > 0
				{
					delTimer(timer_id);
					// delete timer

					setText("");
				}
			}
		}

	private:
		// All subsequent members will be private
		
		// Set some default values/declare variables:
		bool is_running{false};								// timer defaults off
		int timer_id{0};									// timer id of 0 (unintialized)
		std::chrono::steady_clock::time_point start_time;	// declare w/o value

		void initLayout()
		{
			// Defines a function for setup variables

			setText("");  // No default message
			setGeometry(finalcut::FPoint{3,11}, finalcut::FSize{50,1});
			// (Here FPoint is relative to parent dialog) (x,y w,h)

			setAlignment(finalcut::Align::Center);
			// Text put into the label will be center aligned
			
			finalcut::FLabel::initLayout();
			// Run the inheritted class's initLayout (no effect here, but good practice)			
		}
		
		// FWidgets all have in-built function for reacting once timer event occurs
		// To take advantage of this, we must override the function to redefine it:
		void onTimer(finalcut::FTimerEvent* ev) override
		{
			// function takes as input a pointer to object of type FTimerEvent
	
			// double-check timer ID is as expected
			if (ev->getTimerId() != timer_id)
			{
				return;	// uninitialized timer event-- ignore
			}
			
			auto now = std::chrono::steady_clock::now();
			// Get current system clock time (not same as local datetime)
			
			auto elapsed = now - start_time;
			// Get elapsed time as function of current time and start time
			
			auto total_seconds = std::chrono::duration_cast<std::chrono::seconds>(elapsed);
			// Convert elapsed time units to seconds (for easier string setup)
			// duration_cast is a function built in to chrono library for this purpose
			// we tell it our desired format (std::chrono::seconds) and give input (elapsed)
			
			int minutes{(total_seconds.count() % 3600) / 60};
			// Get total minutes into current hour:
			// .count() extracts the raw numeric value from our object, 
			// % gives remainder after division (here after dividing after sec in 1 hr)			
			// technically the % 3600 part is just good/safe practice here--
			// Our timeout is a failsafe to videos can't run that long
			
			int seconds{total_seconds.count() % 60};
			// Get total seconds into current minute:
			
			// Create our new label with this information:
			finalcut::FString time_str;
			time_str.sprintf("Run Time: %02d:%02d", minutes, seconds);
			setText(time_str);
			// First we declare an object of type finalcut::FString
			// Then we use the class's sptrinf method to assign formatted text:
			// In this case, %02d indicates a two digit number (one digit get 0 padded)
			// sprintf style string formatting is common in various languages (ex: MATLAB)
			
			redraw();
			// Now redraw the label with this new info:
			// Normally, we let the parent box redraw everything at once for convienence
			// Here it makes more sense to let the label redraw itself (and only itself)			
		}
};

class MainDialog : public finalcut::FDialog
{
	// Defining a new class to handle our main dialog box
	// "MainDialog" class will inherit from "finalcut::FDialog" class

	public:
		// All subsequent members will be public

		explicit MainDialog (finalcut::FWidget* parent = nullptr)
			 : finalcut::FDialog{parent}
		{
			initLayout();
			initCallbacks();
			// Setup function(s) (defined further down)
			
			updateButtonVisibility();
			// Call button visibility function (defined later) to set initial state
		}

		~MainDialog()
		{
			// Method with ~class name = "destructor"
			// Destructor is called when an object goes out of scope / is deleted
			// They're used to handle any clean up operations

			if (camera_thread.joinable())
			{
				// Check if the camera is currently running

				stop_camera.store(true);
				// If so, tell the camera to shutdown by setting flag

				camera_thread.join();
				// Then wait for it to shutdown before proceeding.
			}
			
		}

	private:
		// All subsequent members will be private
		
		// Initialize child widgets (defined above):
		FileName input{this};				// File name input
		ConfirmButton confirmbutton{this};	// Main button
		YesButton yesbutton{this};			// Yes button
		NoButton nobutton{this};			// No button
		Stopwatch status{this};				// Stopwatch/info
		// Widgets (and their functionality) are initalized here, in the parent--
		// This makes handling inter-widget interaction easier
		
		bool showYesNo{false};
		// Define a boolean to handle what button set is currently visible
		
		std::string std_filename{};
		// Declare the filename variable so it'll be in all subsequent functions' scope
		
		std::thread camera_thread{};
		// Declare the camera's thread variable so it'll be in all subequent functions' scope

		void checkMinValue (int& n)
		{
			// Defines a function that ensures its input is always > 0
			// void tells the compiler the function has no return values
			// That's true because we're directly modifying our input!
			if ( n < 1 ) {
				n = 1 ;
			}
		}

		void initLayout()
		{
			// Defines a function for startup variables
			
			setText ("Reaching Task Camera Control");
			// Set the text in the window label

			auto x = int((getDesktopWidth() - 56) / 2);
			auto y = int((getDesktopHeight() - 15) / 2);
			checkMinValue(x);
			checkMinValue(y);
			// defining x/y values in terms of terminal size and desired window size
			// doing this lets us dynamically prepare our window's position to be centered
			// checkMinValue() ensures values > 0, which would cause errors
			
			setGeometry (finalcut::FPoint{x,y}, finalcut::FSize{56,15});	
			// Here, FPoint is relative to *terminal* (e.g. screen)

			finalcut::FDialog::initLayout();
			// Run the inheritted class's initLayout (no effect here, but good practice)
		}
		
		void initCallbacks()
		{
				confirmbutton.addCallback
				(
					"clicked",					// Callback Signal
					this,						// Instance pointer
					&MainDialog::cb_cbutton		// Member method pointer
				);
				
				yesbutton.addCallback
				(
					"clicked",					// Callback Signal
					this,						// Instance pointer
					&MainDialog::cb_ybutton		// Member method pointer
				);
				
				nobutton.addCallback
				(
					"clicked",					// Callback Signal
					this,						// Instance pointer
					&MainDialog::cb_nbutton		// Member method pointer
				);
		}
		
		void cb_cbutton()
		{
			// Function for handling what happens when we press our main button

			// Check button state:
			if (confirmbutton.getText() == "Start Video")
			{
				// Our click indicates we want to start recording

				auto filename = input.getText();
				// Get the text from our user input
				
				std_filename = filename.toString();
				// By default, filename will be of FString type--
				// For our use, need std::string (standard C++ string type), so convert
				
				// Check filename for issues:
				if (std_filename.empty())
				{
					// filename has bad length
					
					status.setText("ERROR: No file name given");
					// push error message to status:
				}
				else if (std_filename.length() < 4 || std_filename.substr(std_filename.length() - 4) != ".mp4")
				{
					// file does not have proper file extension
					// .substr() gives a substring of the string it is applied to
					// It takes as it's first input a starting index, then goes to end
					// .length() naturally gives the length of our string
					// so by subtracting four, we get last four characters of the string
					// OR comparison ( || ) is to avoid errors if name is less than four characters
					
					status.setText("ERROR: File must have .mp4 extension");
					// push error message to status:
				}
				else if (std::filesystem::exists(std_filename))
				{
					// file already exist
					
					status.setText("WARNING: File already exists. Overwrite?");
					// push warning to status
					
					showYesNo = true;
					// Set flag to swap buttons on redraw
				}
				else
				{
					// no further errors: we can start recording!
					
					StartProtocol();
					// Handle it as a function so "Yes" button can also call it
				}
			}
			else
			{
				// Our click indicates we want to stop recording
				
				stop_camera.store(true);
				// set stop camera flag

				status.setText(finalcut::FString("Stopping..."));
				// set temporary status text so user knows we're mid-process

				status.redraw();
				// redraw to reflect status text:
			}
			
			// Regardless of above, perform updates:
			updateButtonVisibility();
			updateScreen();
		}
		
		void cb_ybutton()
		{
			// Function for handling what happens when we press our "yes" button
			
			showYesNo = false;
			// Return to main button visibility:

			StartProtocol();
			// Start camera/stopwatch protocols:
			
			updateButtonVisibility();
			updateScreen();
			// Perform updates:
		}
		
		void cb_nbutton()
		{
			// Function for handling what happens when we press our "no" button
			
			showYesNo = false;
			status.setText("");
			// Return to default state:						
			
			updateButtonVisibility();
			updateScreen();		
			// Perform updates			
		}
		
		void StartProtocol()
		{
			if (camera_thread.joinable()) {
				// check that camera isn't already starting (in case of a double-click)
				
				return;		// already recording-- don't continue
			}

			try {
				camera_thread = std::thread([this, filename = std_filename]()
				{
					// begin camera
					// std::thread is the class for a C++ thread
					// camera_thread is thus our thread object, and the code in its () its content
					// As input, we give a lambda function that tries out video function
					try {
						VidStart(filename);		// Run camera function
					} catch (std::exception const &e) {
						// Failsafe if VidStart() throws an error we didn't implement catches for
						{
							std::lock_guard<std::mutex> lock(camera_stop_info_mutex);
							camera_stop_info = CameraStopInfo{
								StopType::ERROR,
								std::string("Unexpected exception: ") + e.what()
							};
							// Modify camera_stop_info in mutex
						}
						camera_finished.store(true);	// Let event handler know we're done w/ camera
					}
				});
			} catch (std::exception const &e) {
				// if statement failed-- for some reason, the camera thread wasn't created
				status.setText(finalcut::FString("ERROR: Failed to initialize camera thread"));

				return;		// thread failed-- don't continue past here
			}
				
			status.start();
			// start stopwatch			
			
			confirmbutton.setText("Stop Video");
			// Change main button text to indicate changed functionality:
		}
		
		void StopProtocol()
		{
			if (camera_thread.joinable()) {
				// check if camera_thread is able to be closed:
				stop_camera.store(true);	
				//let camera know we want to stop

				status.setText(finalcut::FString("Stopping..."));
				// Give user an intermitten message so they know we're midprocess

				status.redraw();
				// redraw status (only status) to reflect our change
				
				// Leave rest for onuserEvent to handle			
			}
		}
		
		void updateButtonVisibility()
		{
			confirmbutton.setVisible(!showYesNo);	// confirm button visible if Y/N isn't
			yesbutton.setVisible(showYesNo);		// yes button visible if flag tripped
			nobutton.setVisible(showYesNo);			// no button visible if flag tripped
			
			// Could set focus, but since we're using clicks I don't think its needed?
		}
		
		void updateScreen()
		{
			// update the screen based on our changes:
			redraw();
			// Originally, this function was made assuming the child widgets would call it
			// The logic was far more complicated, involving getting a pointer to parent
			// Check older commits if curious-- there's good instructional value
			// Now however, updates are always done by parent-- hence one-line function
		}

		void onUserEvent(finalcut::FUserEvent* ev) override {
			// onUserEvent is a default method that runs when FWidget is sent a user event
			// Here, we override it to handle being sent camera status events (see below)

			const auto& info = ev ->getData<CameraStopInfo>();
			// Extract info about how camera stopped from the event
			// const auto* info gives us a reference ("info") of compiler deduced type
			// Does this by accessing getData method of finalcut::FUserEvent ev points to
			// <CameraStopInfo> just speciies what type of data we want (can be multiple)

			if (camera_thread.joinable()) {
				// check if camera_thread is able to be closed:
			
				camera_thread.join();
				// make sure camera thread has ended before continuing
				
				camera_thread = std::thread();
				// reset the thread (so it can be properly reused)
			}

			status.stop();	// Stop the stopwatch

			// change status text based on stop reason:
			switch (info.type)
			{
				case StopType::USER: // succesful operation
					status.setText(
						finalcut::FString("Video saved as: ") + 
						finalcut::FString(std_filename)
						);
					break;	// exit switch
				case StopType::TIMEOUT: // timeout
					status.setText(
						finalcut::FString("MAX DURATION. Video saved as: ") +
						finalcut::FString(std_filename)
						);
					break;	// exit switch
				case StopType::ERROR: // video error
					if (info.error_message.empty()) {
						status.setText(finalcut::FString("ERROR: Recording failed"));
						// Recording failed, but no error message (give generic)
					} else {
						status.setText(
							finalcut::FString("ERROR: ") +
							finalcut::FString(info.error_message)
							);
						// Recording failed, send message w/ error
					}
					break;	// either way, exit switch
				default:	// unknown error (catch all)
					status.setText(finalcut::FString("ERROR: Unknown stop reason"));
					break;	// exit switch
			}
			// Use of std_filename here likely still works, but it's clunky and not best practice

			input.setText (finalcut::FString {filename_time()});
			// set new suggested file name as current datetime
			
			confirmbutton.setText("Start Video");
			// Change main button text to indicate changed functionality:
			
			stop_camera.store(false);
			// now that we're all done, set flag to allow another recording:

			redraw();
			// finally, redraw to account for all our changes
			
		}
};

class CameraApplication : public finalcut::FApplication {
	// To handle getting updates based on camera status, need custom FApplication

	public:
		// All subsequent methods will be public
		CameraApplication(const int& argc, char* argv[]) 
			: finalcut::FApplication(argc, argv) { }
		// Copies constructor from main FApplication type
		// Derived classes do *not* automatically inhert constructors!

	private:
		// All subsequent methods will be private

		void processExternalUserEvent() override {
			// proocessExternalUserEvent() runs every FinalCut event loop
			// We can thus override it for our camera status behaviour
			if (camera_finished.load() && getMainWidget()) {
				// Check camera is in finished state and MainDialog exists (safety check)
				camera_finished.store(false);	// reset flag

				CameraStopInfo info;	// declare new CameraStopInfo struct
				{
					std::lock_guard<std::mutex> lock(camera_stop_info_mutex);
					info = camera_stop_info;
					// Access our camera_stop_info for read purposes
					// To keep mutex locked for as little as possible, copy camera_stop_info
					// This will let us work w/ the copied values after scope is over
				}

				finalcut::FUserEvent user_event(finalcut::Event::User, 0);
				// Creates a user event with ID 0
				// We need this user event to take advanatage of onUserEvent()

				user_event.setData(info);
				// Attach the info from our last camera stop to the event

				finalcut::FApplication::sendEvent(getMainWidget(), &user_event);
				// Send the event to main widget (MainDialog) for processing
				// Doing so will trigget onUserEvent()
			}
		}
};

auto main (int argc, char* argv[]) -> int
{
	CameraApplication app{argc, argv};
	// Create the main application object, which manages the FinalCut setup

	MainDialog dialog{&app};
	// Create object of our custom dialog box class, w/ "dialog" as instance name
	// Since we setup our class to initalize it's children, we don't need to do that here
	
	finalcut::FWidget::setMainWidget(&dialog);
	// Sets "dialog" as the main widget for the application.

	dialog.show();
	// Make the widget (and all children) visible.

	return app.exec();
	// Starts the FinalCut main event loop (little confused by the fine details...)	
}
