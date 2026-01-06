#include <final/final.h> //Includes the basic FinalCut library
#include <ctime> //Need for current date/time
#include <string> //Need to use strings

auto filename_time() -> std::string
{
	//Function to get current date-time as character array

	//Get current time as timestamp object
	std::time_t timestamp = std::time(NULL);

	//Convert timestamp into a datetime struct (aka std::tm)
	//The * before localtime is a dereference operator--
	//localtime returns a pointer-- * gives the object it points to
	//Similarly, & before timestamp is a address-of operator
	//localtime wants a pointer for input-- & gives a pointer for an object
	std::tm datetime = *(std::localtime(&timestamp));

	//Allocate space for our string
	char output[80];

	//Write date-time to our char array
	std::strftime(output, 80, "%m-%d-%y_%H-%M-%S.mp4", &datetime);

	return output;
}

class MainButton : public finalcut::FButton
{
	//Defining a new class to handle our main button
	//Colon defines inheretance--
	//"MainButton" class will inherit from "finalcut::FButton" class
	//ie, it will possess all the same members (attributes and methods)
	//"public" indicates public members of inherited class remain public
	//(more on what a public member is later)

	public:
		//All subsequent members will be public

		explicit MainButton (finalcut:FWidget* parent = nullptr)
			 : finalcut:FButton{parent}
		{
			//Method with same name as class = "constructor"
			//Constructor is always called when an object of the class is made
			//"explicit" is a function specifier for conversion functions
			//It tells compilers not to allow implicit type conversions.
			//This is mostly a safety measure to prevent wrong method use

			//This takes a pointer to a Finalcut Widget as input--
			//Specifically, the widget that serves as our button's parent
			//If no input is given, a default null pointer is used.

			//Finally, we have the method inherit from FButton--
			//Specifically, an instance of FButton w/ pointer "parent" as input

			//Past that, we don't need any code in this block (at least here)
			//Since we're inheriting constructor behaviour from FButton

			//Side Note: A "conversion function" converts one type to another.
			//They're declared w/ "operator", or "explicit" for above effect.
			//Any constructor with one input is a conversion function--
			//Since it shows how to convert that input type to our new class.
		}

	private:
		//All subsequent members will be private

		void initLayout()
		{
			//Defines a function for setup variables
		}
}

void cb_cambutton (finalcut::FButton& but, finalcut::FDialog& dgl)
{
	//Function for handling what happens when we press our button
	//Takes as input a reference to an object of type "finalcut::FButton"
	//And a reference to an object of type "finalcut::FDialog"
	//(more on what references are later)

	//Change text of referenced button
	but.setText("&Stop Video");

	//Redraw referenced dialog
	dgl.redraw();
} 

auto main (int argc, char* argv[]) -> int
{
	finalcut::FApplication app(argc, argv);

	//Create "dialog" object w/ type finalcut::FDialog, assigning "app" as parent
	//This type is base class for dialog windows.
	finalcut::FDialog dialog(&app);

	//Setup Dialog Box
	dialog.setText ("Reaching Task Camera Control");
	dialog.setGeometry (finalcut::FPoint{25,5}, finalcut::FSize{60,20});  //x,y w,h


	//Get current-date time as suggested file name
	std::string suggestion = filename_time();

	//Create "input" object w/ type finalcut::FLineEdit, assigning "dialog" as parent
	//String at start will be default input
	finalcut::FLineEdit input(suggestion, &dialog);

	//Setup input object (Here FPoint relative to dialog)
	input.setGeometry (finalcut::FPoint{14,2}, finalcut::FSize{30,1});
	input.setLabelText("File Name: ");


	//Create "label" object w/ type finalcut::FLabel, assigning "dialog" as parent
	finalcut::FLabel label("In the future a timer will go here!", &dialog);

	//Setup label object (Here FPoint relative to dialog)
	label.setGeometry(finalcut::FPoint{2,4}, finalcut::FSize{40,1});


	//Create "button" object w/ type finalcut::FButton, assigning "dialog" as parent
	//String at start will be default input
	finalcut::FButton button("StartVideo", &dialog);

	//Setup button object (Here FPoint relative to dialog)
	button.setGeometry(finalcut::FPoint{20,7}, finalcut::FSize{20,1});

	//Create callback for when button is clicked
	button.addCallback
	(
		"clicked",		//Callback Signal
		&cb_cambutton,		//Function Pointer
		std::ref(button),	//First function argument
		std::ref(dialog)	//Second function argument
	);
	//Side-note: std::ref and std:cref (unused) are helper functions--
	//They generate objects of type std::reference wrapper.
	//As implied, std::reference_wrapper wraps references (via a pointer?)--
	//Allowing them to be treated like objects. (Specifically, being copyable)
	//In turn, a reference is a alternate name for an existing object/variable--
	//It's sort of like a pointer (an alternate way of accessing the same object)--
	//But differs in that it's still the same type as said underlying object.
	//Modifying a reference modifies the underlying variable, and v.versa(?)
	//std::cref holds a const reference (underlying object can't be modified via it)--
	//While std:ref holds a reference that can modifying the underlying object

	//(At the moment, I don't fully get the need for a reference wrapper vs. pointer--
	//Nor reference vs. variable. Other than some upcoming functions expecting them--
	//But then I'm new to C++  ¯\_(ツ)_/¯ )


	//Sets "dialog" as the main widget for the application.
	finalcut::FWidget::setMainWidget(&dialog);

	//Make the widget (and all children) visible.
	dialog.show();

	//Starts the FinalCut main event loop (little confused by the fine details...)
	return app.exec();
}
