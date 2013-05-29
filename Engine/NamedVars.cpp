#include "Include.h"
// ------------------------------------ //
#ifndef LEVIATHAN_NAMEDVARS
#include "NamedVars.h"
#endif
using namespace Leviathan;
#include "Include.h"
// ------------------------------------ //
#include "FileSystem.h"
#include "TimingMonitor.h"
#include "WstringIterator.h"


Leviathan::NamedVar::NamedVar() : Name(L""), wValue(NULL){

	iValue = -1;
	Isint = true;
}

Leviathan::NamedVar::NamedVar(const NamedVar &other){

	Name = other.Name;
	Isint = other.Isint;
	if(Isint){
		iValue = other.iValue;
		wValue = NULL;
	} else {
		iValue = -1;
		wValue = new wstring(*other.wValue);
	}
}

Leviathan::NamedVar::NamedVar(const wstring& name, int val) : Name(name), wValue(NULL){

	iValue = val;
	Isint = true;
}

Leviathan::NamedVar::NamedVar(const wstring& name, const wstring& val) : Name(name), wValue(new wstring(val)){

	iValue = -1;
	Isint = false;
}

DLLEXPORT Leviathan::NamedVar::NamedVar(wstring &line, vector<IntWstring*> *specialintvalues /*= NULL*/) : wValue(NULL){
	// using WstringIterator makes this shorter //
	WstringIterator itr(&line, false);

	unique_ptr<wstring> name = itr.GetUntilEqualityAssignment(EQUALITYCHARACTER_TYPE_ALL);

	if(name->size() < 1){
		// no name //
		throw ExceptionInvalidArguement(L"invalid name", name->size(), __WFUNCSIG__, L"line");
	}

	// get last part of it //
	unique_ptr<wstring> tempvar = itr.GetNextCharacterSequence(UNNORMALCHARACTER_TYPE_CONTROLCHARACTERS);

	if(tempvar->size() < 1){
		// no variable //
		throw ExceptionInvalidArguement(L"no variable", tempvar->size(), __WFUNCSIG__, L"line");
	}
	// remove leading spaces //
	Misc::WstringRemovePreceedingTrailingSpaces(*tempvar);


	Name = *name;

	// check is it a int value //
	if(specialintvalues != NULL){
		bool isspecial = false;
		int specialval = 0;
		// find matching values //
		for(unsigned int indexed = 0; indexed < specialintvalues->size(); indexed++){
			wstring* tempcompare = specialintvalues->at(indexed)->GetString();
			if(tempcompare != NULL){
				if(*tempcompare == *tempvar){
					isspecial = true;
					specialval = specialintvalues->at(indexed)->GetValue();
					break;
				}
			}
		}

		if(isspecial){
			// is int, construct //
			Isint = true;
			iValue = specialval;
			return;
		}
	}

	// check type //
	if(Convert::WstringTypeCheck(*tempvar, 0 /* check int */) == 1){
		// is int //
		Isint = true;
		iValue = Convert::WstringToInt(*tempvar);
		return;
	}
	if(Convert::WstringTypeCheck(*tempvar, 3 /* check bool */) == 1){
		// is int //
		Isint = true;
		iValue = Convert::WstringFromBoolToInt(*tempvar);
		return;
	}
	// is string/other //
	Isint = false;
	iValue = -1;
	if((*tempvar)[0] == L'"' && (*tempvar)[tempvar->size()-1] == L'"'){
		// remove " marks //
		// copy from index 1 to second to last //
		wValue = new wstring(tempvar->substr(1, tempvar->size()-2));

	} else {

		wValue = new wstring(*tempvar);
	}
}

//vector<unsigned int> Deletedindexes;
DLLEXPORT Leviathan::NamedVar::~NamedVar(){

	SAFE_DELETE(wValue);
	//if(Deletedindexes.size() == 0)
	//	Deletedindexes.reserve(2100);

	//// check is this already on deleted list //
	//for(unsigned int i = 0; i < Deletedindexes.size(); i++){
	//	if(Deletedindexes[i] == (unsigned int) this){
	//		__debugbreak();
	//	}
	//}

	//Deletedindexes.push_back((unsigned int)this);
}
// ------------------------------------ //
void Leviathan::NamedVar::SetValue(int val){
	if(this->Isint){
		iValue = val;
		return;
	}
	this->Isint = true;
	// destroy string //
	SAFE_DELETE(wValue);
}

void Leviathan::NamedVar::SetValue(const wstring& val){
	this->Isint = false;
	iValue = -1;
	SAFE_DELETE(wValue);
	wValue = new wstring(val);
}

int Leviathan::NamedVar::GetValue(int& val1, wstring& val2) const{
	if(this->Isint){
		val1 = this->iValue;
		return NAMEDVAR_RETURNVALUE_IS_INT;
	}
	val2 = *this->wValue;
	return NAMEDVAR_RETURNVALUE_IS_WSTRING;
}

DLLEXPORT bool Leviathan::NamedVar::GetValue(int &val) const{
	if(!Isint)
		return false;
	// copy value //
	val = iValue;
	// signal valid value //
	return true;
}

DLLEXPORT bool Leviathan::NamedVar::GetValue(wstring &val) const{
	if(Isint)
		return false;
	// value copying //
	val = *wValue;
	return true;
}

bool Leviathan::NamedVar::IsIntValue() const{
	return Isint;
}

wstring& Leviathan::NamedVar::GetName(){
	return Name;
}

DLLEXPORT void Leviathan::NamedVar::GetName(wstring &name) const{
	// return name in a reference //
	name = Name;
}

void Leviathan::NamedVar::SetName(const wstring& name){
	Name = name;
}

bool Leviathan::NamedVar::CompareName(const wstring& name) const{
	// just default comparison //
	return Name.compare(name) == 0;
}
DLLEXPORT wstring Leviathan::NamedVar::ToText(int WhichSeparator /*= 0*/) const{
	switch(WhichSeparator){
	case 0:
		{
			if(this->Isint)
				return Name+L" = "+Convert::IntToWstring(iValue)+L";";
			return Name+L" = "+*wValue+L";";
		}
	break;
	case 1:
		{
			if(this->Isint)
				return Name+L": "+Convert::IntToWstring(iValue)+L";";
			return Name+L": "+*wValue+L";";
		}
		break;

	default:
		// error //
		QUICK_ERROR_MESSAGE;
		return L"ERROR: NULL";
	}
}

DLLEXPORT NamedVar& Leviathan::NamedVar::operator=(const NamedVar &other){
	// copy values //
	Name = other.Name;
	Isint = other.Isint;
	// check what needs to be copied //
	if(Isint){
		iValue = other.iValue;
		SAFE_DELETE(wValue);
	} else {
		iValue = -1;
		SAFE_DELETE(wValue);
		wValue = new wstring(*other.wValue);
	}

	// return this as result //
	return *this;
}

// ----------------- process functions ------------------- //
DLLEXPORT int Leviathan::NamedVar::ProcessDataDump(const wstring &data, vector<shared_ptr<NamedVar>> &vec, vector<IntWstring*> *specialintvalues /*= NULL*/){
	//QUICKTIME_THISSCOPE;
	// split to lines //
	vector<wstring> Lines;

	if(Misc::CutWstring(data, L";", Lines) != 0){
		// no lines //
		Logger::Get()->Error(L"NamedVar: ProcessDataDump: No lines (even 1 line requires ending ';' to work)", data.length(), true);
		return 400;
	}
	// make space for values //
	vec.clear();
	// let's reserve space //
	vec.reserve(Lines.size());

	// fill values //
	for(unsigned int i = 0; i < Lines.size(); i++){
		// skip empty lines //
		if(Lines.at(i).length() == 0)
			continue;

		// create a named var //
		try{
			vec.push_back(shared_ptr<NamedVar>(new NamedVar(Lines[i], specialintvalues)));
		}
		catch (const ExceptionInvalidArguement &e){
			// print to log //
			e.PrintToLog();
			// exception throws, must be invalid line //
			Logger::Get()->Info(L"NamedVar: ProcessDataDump: contains invalid line, line (with only ASCII characters): "+
				Convert::StringToWstring(Convert::WstringToString(Lines[i]))+L"\nEND", false);
			continue;
		}

		// check is it valid //
		if(vec.back()->GetName().size() == 0 || vec.back()->GetName().size() > 10000){
			// invalid //
			Logger::Get()->Error(L"NamedVar: ProcessDataDump: invalid NamedVar generated for line: "+Lines[i]+L"\nEND");
			DEBUG_BREAK;
			vec.erase(vec.end());
		}

		continue;
	}

	return 0;
}



DLLEXPORT  void Leviathan::NamedVar::SwitchValues(NamedVar &receiver, NamedVar &donator){
	// only overwrite name if there is one //
	if(donator.Name.size() > 0)
		receiver.Name = donator.Name;

	receiver.Isint = donator.Isint;
	SAFE_DELETE(receiver.wValue);
	receiver.iValue = -1;
	if(receiver.Isint){
		// copy ivalue //
		receiver.iValue = donator.iValue;
	} else {
		// copy string pointers around //
		receiver.wValue = donator.wValue;
	}
	// don't allow original owner to delete this //
	donator.wValue = NULL;

	// maybe butcher donator entirely //
	donator.iValue = -2;
}

// ---------------------------- NamedVars --------------------------------- //
Leviathan::NamedVars::NamedVars() : Variables(){
	// nothing to initialize //
}
Leviathan::NamedVars::NamedVars(const NamedVars& other){
	// deep copy is required here //
	Variables.reserve(other.Variables.size());
	for(unsigned int i = 0; i < other.Variables.size(); i++){
		Variables.push_back(shared_ptr<NamedVar>(new NamedVar(*other.Variables[i])));
	}
}
DLLEXPORT Leviathan::NamedVars::NamedVars(const wstring &datadump) : Variables(){

	// load data directly to vector //
	if(NamedVar::ProcessDataDump(datadump, Variables, NULL) != 0){
		// error happened //
		Logger::Get()->Error(L"NamedVars: Initialize: process datadump failed", true);
	}
}

Leviathan::NamedVars::~NamedVars(){
	// clear values //
	Variables.clear();
}
		// ------------------------------------ //
bool Leviathan::NamedVars::SetValue(const wstring &name, int val){
	int index = Find(name);
	ARR_INDEX_CHECKINV(index, (int)Variables.size()){
		Logger::Get()->Warning(L"NamedVars: SetValue: not found, creating new for: "+name, false);

		this->AddVar(name, val, L"", true);
		return true;
	}
	// set value //
	Variables[index]->SetValue(val);
	return true;
}

bool Leviathan::NamedVars::SetValue(const wstring &name, wstring &val){
	int index = Find(name);
	ARR_INDEX_CHECKINV(index, (int)Variables.size()){
		Logger::Get()->Warning(L"NamedVars: SetValue: not found, creating new for: "+name, false);

		this->AddVar(name, 0, val, true);
		return true;
	}
	// set value //
	Variables[index]->SetValue(val);
	return true;
}

DLLEXPORT bool Leviathan::NamedVars::SetValue(NamedVar &nameandvalues){
	int index = Find(nameandvalues.Name);
	// index check //
	ARR_INDEX_CHECKINV(index, (int)Variables.size()){
		Logger::Get()->Warning(L"NamedVars: SetValue: not found, creating new for: "+nameandvalues.GetName(), false);

		Variables.push_back(shared_ptr<NamedVar>(new NamedVar(nameandvalues)));
		return true;
	}
	nameandvalues.Name.clear();
	// set values with "swap" //
	NamedVar::SwitchValues(*Variables[index].get(), nameandvalues);
	return true;
}

int Leviathan::NamedVars::GetValue(const wstring &name, int& val1, wstring& val2) const{
	int index = Find(name);
	// index check //
	ARR_INDEX_CHECKINV(index, (int)Variables.size()){
		Logger::Get()->Error(L"NamedVars: GetValue: out of range, trying to find: "+name, index);
		return 6;
	}

	return Variables[index]->GetValue(val1, val2);
}

int Leviathan::NamedVars::GetValue(const wstring &name, int& val1) const{
	int index = Find(name);
	// index check //
	ARR_INDEX_CHECKINV(index, (int)Variables.size()){
		Logger::Get()->Error(L"NamedVars: GetValue: out of range, trying to find: "+name, index);
		return 6;
	}

	return Variables[index]->GetValue(val1);
}

DLLEXPORT int Leviathan::NamedVars::GetValue(const wstring &name, wstring& val) const{
	int index = Find(name);
	// index check //
	ARR_INDEX_CHECKINV(index, (int)Variables.size()){
		Logger::Get()->Error(L"NamedVars: GetValue: out of range, trying to find: "+name, index);
		return 6;
	}

	return Variables[index]->GetValue(val);
}


bool Leviathan::NamedVars::IsIntValue(const wstring &name) const{
	// call overload //
	return IsIntValue((unsigned int)Find(name));
}
bool Leviathan::NamedVars::IsIntValue(unsigned int index) const{
	ARR_INDEX_CHECKINV(index, Variables.size()){
		Logger::Get()->Error(L"NamedVars: IsIntValue: out of range", index);
		return false;
	}

	return Variables[index]->IsIntValue();
}

wstring& Leviathan::NamedVars::GetName(unsigned int index){
	ARR_INDEX_CHECKINV(index, Variables.size()){
		Logger::Get()->Error(L"NamedVars: GetName: out of range", index);
		//wstring errstr = Misc::GetErrString();
		return Misc::GetErrString();
	}

	return Variables[index]->GetName();
}

DLLEXPORT bool Leviathan::NamedVars::GetName(unsigned int index, wstring &name) const{
	ARR_INDEX_CHECKINV(index, Variables.size()){
		Logger::Get()->Error(L"NamedVars: GetName: out of range", index);
		return false;
	}
	// fetch name to variables //
	Variables[index]->GetName(name);
	return true;
}

void Leviathan::NamedVars::SetName(unsigned int index, const wstring &name){
	ARR_INDEX_CHECKINV(index, Variables.size()){
		Logger::Get()->Error(L"NamedVars: SetName: out of range", index);
		return;
	}

	Variables[index]->SetName(name);
}
void Leviathan::NamedVars::SetName(const wstring &oldname, const wstring &name){
	// call overload //
	SetName(Find(oldname), name);
}
bool Leviathan::NamedVars::CompareName(unsigned int index, const wstring &name) const{
	ARR_INDEX_CHECK(index, Variables.size())
		return Variables[index]->CompareName(name);
	// maybe throw an exception here //
	DEBUG_BREAK;
	return false;
}
void Leviathan::NamedVars::AddVar(const wstring &name, int val, const wstring &wval, bool isint){
	if(isint){
		Variables.push_back(shared_ptr<NamedVar>(new NamedVar(name, val)));
	} else {
		Variables.push_back(shared_ptr<NamedVar>(new NamedVar(name, wval)));
	}
}

DLLEXPORT void Leviathan::NamedVars::AddVar(shared_ptr<NamedVar> values){
	// just add to vector //
	Variables.push_back(values);
}

void Leviathan::NamedVars::Remove(unsigned int index){
	ARR_INDEX_CHECKINV(index, Variables.size()){
		Logger::Get()->Error(L"NamedVars: Remove: out of range", index);
		return;
	}
	// smart pointers //
	Variables.erase(Variables.begin()+index);
}

DLLEXPORT void Leviathan::NamedVars::Remove(const wstring &name){
	// call overload //
	Remove(Find(name));
}


int Leviathan::NamedVars::LoadVarsFromFile(const wstring &file){
	return FileSystem::LoadDataDumb(file, Variables);
}
vector<shared_ptr<NamedVar>>* Leviathan::NamedVars::GetVec(){
	return &Variables;
}
void Leviathan::NamedVars::SetVec(vector<shared_ptr<NamedVar>>& vec){
	Variables = vec;
}
// ------------------------------------ //
DLLEXPORT int Leviathan::NamedVars::Find(const wstring &name) const{
	for(unsigned int i = 0; i < Variables.size(); i++){
		if(Variables[i]->CompareName(name))
			return i;
	}
#ifdef _DEBUG
	Logger::Get()->Warning(L"NamedVars: Find: \""+name+L"\" not found");
#endif // _DEBUG
	return -1;
}
