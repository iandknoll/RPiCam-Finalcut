// FUTURE NOTE: I wrote this code in part as a learning experience for C++
// I did my best to follow repostory implementations, bug test, and error handle
// Nevertheless, there are likely to be shortcomings and issues not fully addressed
// As a matter of fact, I *know* both exist, though I believe them to be minor
// If you are maintaining this, I grant full permission to mock my feeble efforts

// CAMERA DEPENDENCIES
#include "core/rpicam_encoder.hpp" 	// Contains code for encoding video w/ rpicam
#include "core/logging.hpp"			// Contains code for the logginc macro used
#include "output/output.hpp"  		// Contains code for outputing video to file
#include <functional>				// Needed to use std::bind

// FRONT END DEPENDENCIES
#include <final/final.h> 			// Includes the basic FinalCut library
#include <ctime> 					// Need for current date/time
#include <chrono>					// Need for stopwatch
#include <filesystem> 				// Need to check if file(s) exist

// SHARED DEPENDENCIES
#include <string>					// Required for std::string type
#include <atomic>					// Required for our global variable
#include <thread>					// Required for threading


// GLOBALS
static std::atomic<bool> stop_camera(false);	// Declare variable for signal handling
// In this context, "static" means variable is allocated once, for lifetime of program
// We will run our camera on a seperate thread to the TUI (so they can operate parallel)
// Because of this, anything both can access needs to be set as atomic--
// In brief, this prevents other threads from seeing mid-write (garbage) states.
// This is what we're doing here with the "std::atomic<bool>" part

enum class StopType {
	USER = 0,
	TIMEOUT = 1,
	ERROR = -1
};
// enum: user defined data type assigning names to integer values
// mainly for readability-- using here to have plaintext names for function returns


// CAMERA
static int get_colourspace_flags(std::string const &codec) {
	// Function for handling encoder colour space
	// Copied from rpicam_vid.cpp-- it being static there means we can't reference it
	if (codec == "mjpeg" || codec == "yuv420") {
		// if codec uses a jpeg colorspace...
		return RPiCamEncoder::FLAG_VIDEO_JPEG_COLOURSPACE; // return with such
	}
	else {
		return RPiCamEncoder::FLAG_VIDEO_NONE; // otherwise, nothing needed(?)
	}
}


static StopType VidStart(std::string const& name) {
	// Define function for running rpicam-vid
	// static means the function can't be called outside this source file
	// StopType is the expected type of any return variable (defined later)
	// "std::string const& name" passes a reference to a variable of type string as input
	// passing by reference is faster and more memory efficient than passing by value
	// This is because passing by value requires creating a copy of the variable
	// BUT, using a reference means modifying the parameter WILL affect the original
	// (A reference is just another variable name for the same object in memory)
	// That said, const means the function won't have write access on the variable

	// (Side-Note: :: is a "scope resolution operator". If x::y then:)
	// (x defines a scope-- a region of code where a variable/identifier is accessible)
	// (Many Scopes exist, including Global or Local-- e.g. between any given set of {} )
	// (Here, the scope is a Namespace-- which libaries use to avoid variable conflict)
	// (y then defines an identifer-- a type, function, variable, etc. in said scope)
	// (x::y then just lets us access identifier x in scope y)	

	bool EncoderOn{false};
	bool CameraOn{false};
	// Setup some variables to track Camera/Encoder status for exception handling

	// Side-Note: The use of {} in the above is known as braced initilization
	// As opposed to more traditional assignment initializion (e.g. int x = 1)
	// Generally, this is the prefered initilization method in C++
	// The main reason is it prevents unintended narrowing conversion
	// Narrowing conversion results when going from a wider type to a narrower type
	// For example, trying to assign the float 3.5 to an int data type
	// w/ assignment initilization, the result is 3 (a loss in data!)
	// w/ braced initilization, you would get a compilation error for trying to do this

	RPiCamEncoder app;
	// Creates an object of the class "RPiCamEncoder" with name "app"
	// RPiCamEncoder handles the video recording pipeline
	// We do this outside of the try block so it's in scope for catch
	
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
		
		// (Side-Note 2: a struct differs from a class in that members are public by default)
		// (This means that you can directly access them from outside of the struct)
		// (In a class, members must be explicitly set as public, otherwise they're private)
		// (The later  means they can only be accessed from outside in special circumstances)

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
		// the arrow operator -> dereferences a pointer (getting the unnderlying object),
		// then accesses the specified member of the class on the right hand side of arrow
		// here "member" just means a variable, function, etc. defined inside a class/struct


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
		// and the four afformentioned frame parameters as inputs."

		app.SetMetadataReadyCallback(std::bind(&Output::MetadataReady, output.get(), _1));
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
				if (CameraOn) {
					app.StopCamera();	// Camera should be on, so stop it
				}
				if (EncoderOn) {
					app.StopEncoder();	// Encoder should be on, so stop it
				}
				return StopType::USER;	// End loop early, user as reason
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
					LOG_ERROR("ERROR: Camera restart failed: " << e.what());
					// Log the issue in terminal/log file (depending on how program ran)
	
					if (CameraOn) {
						app.StopCamera();	// Camera should be on, so stop it
					}
					if (EncoderOn) {
						app.StopEncoder();	// Encoder should be on, so stop it
					}
					return StopType::ERROR;		// End loop early, error as reason
				}
				continue;
			}
			else if (msg.type != RPiCamEncoder::MsgType::RequestComplete)	// broad error check
			{
				if (CameraOn) {
					app.StopCamera();	// Camera should be on, so stop it
				}
				if (EncoderOn) {
					app.StopEncoder();	// Encoder should be on, so stop it
				}
				return StopType::ERROR;		// End loop early, error as reason
			}

			auto now = std::chrono::high_resolution_clock::now();	// Get current time
			if ((now - start_time) > options->timeout.value)	// If elapsed time > timeout
			{
				if (CameraOn) {
					app.StopCamera();	// Camera should be on, so stop it
				}
				if (EncoderOn) {
					app.StopEncoder();	// Encoder should be on, so stop it
				}
				return StopType::TIMEOUT;		// End loop early, timeout as reason
			}

			if (stop_camera.load()) // If the user send a shutdown signal
			{
				//Note: .load() is the how you read an atomic variable
				
				if (CameraOn) {
					app.StopCamera();	// Camera should be on, so stop it
				}
				if (EncoderOn) {
					app.StopEncoder();	// Encoder should be on, so stop it
				}
				return StopType::USER;	// End loop early, user as reason
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

		if (CameraOn) {
			app.StopCamera();	// Camera should be on, so stop it
		}
		if (EncoderOn) {
			app.StopEncoder();	// Encoder should be on, so stop it
		}
		
		LOG_ERROR("ERROR: *** " << e.what() << " ***");
		// Use LOG_ERROR() macro to send error to log/terminal (depending on how code is run)
		// e.what() is a method for type std::exception to outputs error as a string pointer

		// (Side-Note: A macro is a placeholder for preprocessor to replace before compilation)
		// (A macro can be an object/variable, function (as here), or conditional)
		// (It speed up development by reusing code w/o function calls or explicit rewrites)

		return StopType::ERROR;
		// If an error occured, end script and indicate error as stop reason
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

class MainDialog : public finalcut::FDialog
{
	// Defining a new class to handle our main dialog box
	// Colon defines inheretance--
	// "MainButton" class will inherit from "finalcut::FButton" class
	// ie, it will possess all the same members (objects and methods)
	// "public" indicates public members of inherited class remain public

	public:
		// All subsequent members will be public

		explicit MainDialog (finalcut::FWidget* parent = nullptr)
			 : finalcut::FDialog{parent}
		{
			// Method with same name as class = "constructor"
			// Constructor is always called when an object of the class is made
			// "explicit" is a function specifier for conversion functions
			// It tells compilers not to allow implicit type conversions.
			// This is mostly a safety measure to prevent wrong method use

			// This takes a pointer to a Finalcut Widget as input--
			// Specifically, the widget that serves as our dialog's parent
			// If no input is given, a default null pointer is used.

			// Finally, we have the method inherit from FDialog--
			// Specifically, an instance of FDialog w/ pointer "parent" as input

			// Side Note: A "conversion function" converts one type to another.
			// They're declared w/ "operator", or "explicit" for above effect.
			// Any constructor with one input is a conversion function--
			// Since it shows how to convert that input type to our new class.

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
				// .store() is the method for writing to atomic variable

				camera_thread.join();
				// Then wait for it to shutdown before proceeding.
			}
			
		}

	private:
		// All subsequent members will be private
		
		// Initialize child widgets (defined elsewhere):
		FileName input{this};				// File name input
		ConfirmButton confirmbutton{this};	// Main button
		YesButton yesbutton{this};			// Yes button
		NoButton nobutton{this};			// No button
		Stopwatch status{this};				// Stopwatch/info
		// Widgets (and their functionality) are initalized here, in the parent--
		// This makes handling inter-widget interaction easier
		
		bool showYesNo{false};
		// Define a boolean to handle what button set is currently visible
		
		std::string suggestion = filename_time();
		// Declare variable for input text so it'll be in all subsequent functions' scope
		
		std::string std_filename;
		// Declare the filename variable so it'll be in all subsequent functions' scope
		
		std::thread camera_thread;
		// Declare the camera's thread variable so it'll be in all subequent functions' scope

		std::atomic<StopType> last_stop_reason_{StopType::USER};
		// Declare an atomic variable to handle returns from camera
		
		void initLayout()
		{
			// Defines a function for startup variables
			// "void" tells the compiler the function has no return value
			
			setText ("Reaching Task Camera Control");
			setGeometry (finalcut::FPoint{25,5}, finalcut::FSize{60,20});	
			// finalcut::FPoint{x,y} handles where the top right corner of a widget goes
			// finalcut::FPoint{w,h} handles the width and height of the widget
			// In both cases, the units are in terms of standard-size text spaces

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
				
				StopProtocol();
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
			
			camera_thread = std::thread([this, filename = std_filename]()
			{
				// begin camera
				// std::thread is the class for a C++ thread
				// camera_thread is thus our thread object, and the code in its () its content
				// As input, we give a lambda function (synonymous with anonymous function*)
				// lambda functions use format []() {}:
				// [] contains any variables the function will need in its operations
				// () describes the input argument types/names (like a regular function)
				// {} contains the actual code the lambda function will run
				
				StopType result = VidStart(filename);
				// Run our camera function, storing return value in "result:"				

				last_stop_reason_.store(result);
				// Save result to atomic last_stop_reason, so other threads can reference				
			});
			
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
				// set stop camera flag

				status.setText(finalcut::FString("Stopping..."));
				// set temporary status text so user knows we're mid-process

				status.redraw();
				// redraw to reflect status text:
			
				camera_thread.join();
				// make sure camera thread has ended before continuing
				
				camera_thread = std::thread();
				// reset the thread (so it can be properly reused)
				
				status.stop();
				// reset stopwatch
	
				StopType reason = last_stop_reason_.load();
				// get the stop reason:
	
				// change status text based on stop reason:
				switch (reason)
				{
					case StopType::USER: // succesful operation
						status.setText(finalcut::FString("Video saved as: ") 
									   << finalcut::FString(std_filename));
						break;	// exit switch
					case StopType::TIMEOUT: // timeout
						status.setText(finalcut::FString("MAX DURATION REACHED. Video saved as: ") 
									   << finalcut::FString(std_filename));
						break;	// exit switch
					case StopType::ERROR: // video error
						status.setText(finalcut::FString("ERROR: Recording failed"));
						break;	// exit switch
					default:	// unknown error (catch all)
						status.setText(finalcut::FString("ERROR: Unknown stop reason"));
						break;	// exit switch
				}
							
				suggestion = filename_time();
				// Get current date-time as suggested file name
				
				input.setText (finalcut::FString {suggestion});	//Need to convert type(?)
				// set new suggested file name:
				
				confirmbutton.setText("Start Video");
				// Change main button text to indicate changed functionality:
				
				stop_camera.store(false);
				// now that we're all done, set flag to allow another recording:
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
			
			auto parent_dialog = static_cast<finalcut::FDialog*>(getParent());
			// First, we get a pointer to the parent widget using getParent()
			// Initially, the pointer is to type FWidget*, but we need FDialog*
			// This is accomplished via static_cast<finalcut::FDialog*>
			// Finally, we store that pointer as parent_dialog--
			// Using auto to allow the compiller to deduce the correct type			

			// Null pointers evaluate as false, so check we got pointer succesfully:
			if(parent_dialog)
			{
				parent_dialog->redraw();
				// If so, use that pointer to redraw the parent dialog
				
				// Side-Note 1: When trying to access a class method:
				// "->" is for pointers, while "." is for objects

				// Side-Note 2: In C++, one-line if statements do NOT require {}
				// For consistency, safety, and familarity, I always include them
			}
		}
};

class ConfirmButton : public finalcut::FButton
{
	// Defining a new class to handle our main button
	// It will inherit properties from the "finalcut::FButton" class

	public:
		// All subsequent members will be public

		explicit ConfirmButton (finalcut::FWidget* parent = nullptr)
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
			
			setText ("Start Video");
			setGeometry(finalcut::FPoint{20,7}, finalcut::FSize{20,1});
			// (Here FPoint is relative to parent dialog) (x,y w,h)			

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
			setGeometry(finalcut::FPoint{20,7}, finalcut::FSize{8,1});
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
			setGeometry(finalcut::FPoint{32,7}, finalcut::FSize{8,1});
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
			
			std::string fsuggestion = filename_time();
			// Get current date-time as suggested file name

			setText (finalcut::FString{fsuggestion});	// Need to convert type(?)
			setGeometry (finalcut::FPoint{14,2}, finalcut::FSize{30,1});
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
			setGeometry(finalcut::FPoint{20,9}, finalcut::FSize{40,1});
			// (Here FPoint is relative to parent dialog) (x,y w,h)

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
			
			int minutes = (total_seconds.count() % 3600) / 60;
			// Get total minutes into current hour:
			// .count() extracts the raw numeric value from our object, 
			// % gives remainder after division (here after dividing after sec in 1 hr)			
			// technically the % 3600 part is just good/safe practice here--
			// Our timeout is a failsafe to videos can't run that long
			
			int seconds = total_seconds.count() % 60;
			// Get total seconds into current minute:
			
			// Create our new label with this information:
			setText(finalcut::FString("Video Length:")
					.setWidth(2) << minutes << ":"
					.setWidth(2) << seconds);
			// First, we create an initial object of type finalcut::FString--
			// Elsewhere setText() can perform implicit conversion from char/string,
			// but here we want finalcut::FString for its special formatting abilities:
			// .setWidth() for padding numbers (here to always be display w/ two digits),
			// and <<, which lets us append the int/string to our finalcut::FString object
			
			// Side-Note: The use of "<<" here is an example of an overloaded operator:
			// A C++ feature that lets us redefine what an operator does to an object type.
			// Normally, << should perform a bitwise left shift (which I won't explain here)
			// finalcut::FString is written to reuse that operator for appending instead
			
			redraw();
			// Now redraw the label with this new info:
			// Normally, we let the parent box redraw everything at once for convienence
			// Here it makes more sense to let the label redraw itself (and only itself)			
		}
};


auto main (int argc, char* argv[]) -> int
{
	finalcut::FApplication app(argc, argv);
	// Create the main application object, which manages the Finalcut setup

	MainDialog dialog(&app);
	// Create object of our custom dialog box class, w/ "dialog" as instance name
	// Since we setup our class to initalize it's children, we don't need to do that here
	
	finalcut::FWidget::setMainWidget(&dialog);
	// Sets "dialog" as the main widget for the application.

	dialog.show();
	// Make the widget (and all children) visible.

	return app.exec();
	// Starts the FinalCut main event loop (little confused by the fine details...)	
}
