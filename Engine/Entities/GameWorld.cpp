#include "Include.h"
// ------------------------------------ //
#ifndef LEVIATHAN_GAMEWORLD
#include "GameWorld.h"
#endif
#include "Rendering/GraphicalInputEntity.h"
#include "Newton/NewtonManager.h"
#include "OgreRoot.h"
#include "OgreSceneManager.h"
#include "OgreLight.h"
#include "OgreSceneNode.h"
#include "OgreCamera.h"
#include "OgreViewport.h"
#include "Compositor/OgreCompositorWorkspace.h"
#include "Compositor/OgreCompositorManager2.h"
using namespace Leviathan;
// ------------------------------------ //
DLLEXPORT Leviathan::GameWorld::GameWorld() : WorldSceneCamera(NULL), WorldsScene(NULL), Sunlight(NULL), SunLightNode(NULL), WorldFrozen(false), 
	GraphicalMode(false), LinkedToWindow(NULL)
{

}

DLLEXPORT Leviathan::GameWorld::~GameWorld(){

}
// ------------------------------------ //
DLLEXPORT bool Leviathan::GameWorld::Init(GraphicalInputEntity* renderto, Ogre::Root* ogre){

	LinkedToWindow = renderto;

	// Detecting non-GUI mode //
	if(ogre){
		if(!renderto)
			return false;
		GraphicalMode = true;
		// these are always required for worlds //
		_CreateOgreResources(ogre, renderto->GetWindow());
	}

	// acquire physics engine world //
	_PhysicalWorld = NewtonManager::Get()->CreateWorld(this);

	return true;
}

DLLEXPORT void Leviathan::GameWorld::Release(){
	GUARD_LOCK_THIS_OBJECT();

	// release objects //
	for(size_t i = 0; i < Objects.size(); i++){

		Objects[i]->ReleaseData();
	}

	Objects.clear();

	if(GraphicalMode){
		// TODO: notify our window that it no longer has a world workspace
		LinkedToWindow = NULL;

		// release Ogre resources //

		// Destroy the compositor //
		Ogre::Root& ogre = Ogre::Root::getSingleton();
		ogre.getCompositorManager2()->removeWorkspace(WorldWorkspace);
		WorldWorkspace = NULL;
		ogre.destroySceneManager(WorldsScene);
		WorldsScene = NULL;
	}

	// some smart ptrs need releasing //
	_PhysicalWorld.reset();

}
// ------------------------------------ //
void Leviathan::GameWorld::_CreateOgreResources(Ogre::Root* ogre, Window* rendertarget){
	// create scene manager //
	WorldsScene = ogre->createSceneManager(Ogre::ST_EXTERIOR_FAR, 2, Ogre::INSTANCING_CULLING_THREADED, "MainSceneManager");

	WorldsScene->setShadowFarDistance(1000.f);
	WorldsScene->setShadowDirectionalLightExtrusionDistance(10000.f);

	// create camera //
	WorldSceneCamera = WorldsScene->createCamera("Camera01");

	// near and far clipping planes //
	WorldSceneCamera->setFOVy(Ogre::Radian(60.f*DEGREES_TO_RADIANS));
	WorldSceneCamera->setNearClipDistance(0.1f);
	WorldSceneCamera->setFarClipDistance(50000.f);

	// enable infinite far clip distance if supported //
	if(ogre->getRenderSystem()->getCapabilities()->hasCapability(Ogre::RSC_INFINITE_FAR_PLANE)){
		
		WorldSceneCamera->setFarClipDistance(0);
	}

	// set scene ambient colour //
	WorldsScene->setAmbientLight(Ogre::ColourValue(0.3f, 0.3f, 0.3f));

	// default sun //
	SetSunlight();
	
	// Create the workspace for this scene //
	// Which will be rendered before the overlay workspace //
	WorldWorkspace = ogre->getCompositorManager2()->addWorkspace(WorldsScene, rendertarget->GetOgreWindow(), 
		WorldSceneCamera, "WorldsWorkspace", true, 0);
}
// ------------------------------------ //
DLLEXPORT void Leviathan::GameWorld::SetSkyBox(const string &materialname){
	try{
		WorldsScene->setSkyBox(true, materialname);
	}
	catch(const Ogre::InvalidParametersException &e){

		Logger::Get()->Error(L"[EXCEPTION] "+Convert::StringToWstring(e.getFullDescription()));
	}
}

DLLEXPORT void Leviathan::GameWorld::SetFog(){
	WorldsScene->setFog(Ogre::FOG_LINEAR, Ogre::ColourValue(0.7f, 0.7f, 0.8f), 0, 4000, 10000);
	WorldsScene->setFog(Ogre::FOG_NONE);
}

DLLEXPORT void Leviathan::GameWorld::SetSunlight(){
	// create/update things if they are NULL //
	if(!Sunlight){
		Sunlight = WorldsScene->createLight();
		Sunlight->setName("sunlight");
	}

	Sunlight->setType(Ogre::Light::LT_DIRECTIONAL);
	Sunlight->setDiffuseColour(0.98f, 1.f, 0.95f);
	Sunlight->setSpecularColour(1.f, 1.f, 1.f);

	if(!SunLightNode){

		SunLightNode = WorldsScene->getRootSceneNode()->createChildSceneNode();
		SunLightNode->setName("sunlight node");

		SunLightNode->attachObject(Sunlight);
	}

	Ogre::Quaternion quat;
	quat.FromAngleAxis(Ogre::Radian(1.f), Float3(0.55f, -0.3f, 0.75f));
	SunLightNode->setOrientation(quat);
}

DLLEXPORT void Leviathan::GameWorld::RemoveSunlight(){
	if(SunLightNode){
		SunLightNode->detachAllObjects();
		// might be safe to delete
		OGRE_DELETE SunLightNode;
		SunLightNode = NULL;
	}
}

DLLEXPORT void Leviathan::GameWorld::UpdateCameraLocation(int mspassed, ViewerCameraPos* camerapos, ObjectLock &guard){
	VerifyLock(guard);
	// Skip if no camera //
	if(camerapos == NULL)
		return;

	// update camera //
	camerapos->UpdatePos(mspassed);

	// set camera position //
	WorldSceneCamera->setPosition(camerapos->GetPosition());

	// convert rotation into a quaternion //
	const Float3& angles = camerapos->GetRotation();

	// create quaternion from quaternion rotations around each axis //
	Ogre::Quaternion rotq(Ogre::Degree(angles.Y), Ogre::Vector3::UNIT_X);
	Ogre::Quaternion rotyaw(Ogre::Degree(angles.X), Ogre::Vector3::UNIT_Y);
	Ogre::Quaternion rotroll(Ogre::Degree(angles.Z), Ogre::Vector3::UNIT_Z);

	rotq = rotyaw*rotq*rotroll;

	WorldSceneCamera->setOrientation(rotq);
}
// ------------------ Object managing ------------------ //
DLLEXPORT void Leviathan::GameWorld::AddObject(BaseObject* obj){
	AddObject(shared_ptr<BaseObject>(obj, SharedPtrReleaseDeleter<BaseObject>::DoRelease));
}

DLLEXPORT void Leviathan::GameWorld::AddObject(shared_ptr<BaseObject> obj){
	GUARD_LOCK_THIS_OBJECT();
	Objects.push_back(obj);
}

DLLEXPORT shared_ptr<BaseObject> Leviathan::GameWorld::GetWorldObject(int ID){
	// ID shouldn't be under zero //
	if(ID == -1){

		Logger::Get()->Warning(L"GameWorld: GetWorldObject: trying to find object with ID == -1 (IDs shouldn't be negative)");
		return nullptr;
	}

	GUARD_LOCK_THIS_OBJECT();

	for(std::vector<shared_ptr<BaseObject>>::iterator iter = Objects.begin(); iter != Objects.end(); ++iter){
		if((*iter)->GetID() == ID){
			return *iter;
		}
	}
	return nullptr;
}


DLLEXPORT void Leviathan::GameWorld::ClearObjects(ObjectLock &guard){
	VerifyLock(guard);

	for(std::vector<shared_ptr<BaseObject>>::iterator iter = Objects.begin(); iter != Objects.end(); ++iter){
		// Release the object //
		(*iter)->ReleaseData();
	}
	// Release our reference //
	Objects.clear();
}


DLLEXPORT Float3 Leviathan::GameWorld::GetGravityAtPosition(const Float3 &pos){
	// \todo take position into account //
	// create force without mass applied //
	return Float3(0.f, PHYSICS_BASE_GRAVITY, 0.f);
}
// ------------------------------------ //
DLLEXPORT void Leviathan::GameWorld::SimulateWorld(){
	GUARD_LOCK_THIS_OBJECT();
	// This is probably the best place to remove dead objects //
	_HandleDelayedDelete(guard);

	// Only if not frozen run physics //
	if(!WorldFrozen)
		_PhysicalWorld->SimulateWorld();
}

DLLEXPORT void Leviathan::GameWorld::ClearSimulatePassedTime(){
	_PhysicalWorld->ClearTimers();
}
// ------------------------------------ //
DLLEXPORT void Leviathan::GameWorld::DestroyObject(int ID){
	GUARD_LOCK_THIS_OBJECT();
	for(std::vector<shared_ptr<BaseObject>>::iterator iter = Objects.begin(); iter != Objects.end(); ++iter){
		if((*iter)->GetID() == ID){
			// release the object and then erase our reference //
			(*iter)->ReleaseData();
			Objects.erase(iter);
			return;
		}
	}
}

DLLEXPORT void Leviathan::GameWorld::QueueDestroyObject(int ID){
	GUARD_LOCK_THIS_OBJECT();
	DelayedDeleteIDS.push_back(ID);
}

void Leviathan::GameWorld::_HandleDelayedDelete(ObjectLock &guard){

	VerifyLock(guard);

	// We might want to delete everything //
	if(ClearAllObjects){


		ClearObjects();

		ClearAllObjects = false;
		// All are now cleared //
		return;
	}

	// Return right away if no objects to delete //
	if(DelayedDeleteIDS.size() == 0)
		return;

	// Search all objects and find the ones that need to be deleted //
	for(auto iter = Objects.begin(); iter != Objects.end(); ){

		// Check does id match any //
		int curid = (*iter)->GetID();
		bool delthis = false;

		for(auto iterids = DelayedDeleteIDS.begin(); iterids != DelayedDeleteIDS.end(); ){

			if(*iterids == curid){
				// Remove this as it will get deleted //
				delthis = true;
				DelayedDeleteIDS.erase(iterids);
				break;

			} else {
				++iterids;
			}
		}

		if(delthis){

			(*iter)->ReleaseData();
			iter = Objects.erase(iter);
			// Check for end //
			if(DelayedDeleteIDS.size() == 0)
				return;

		} else {
			++iter;
		}
	}

}
// ------------------------------------ //
DLLEXPORT void Leviathan::GameWorld::SetWorldPhysicsFrozenState(bool frozen){
	// Skip if set to the same //
	if(frozen == WorldFrozen)
		return;

	GUARD_LOCK_THIS_OBJECT();

	WorldFrozen = frozen;
	// If unfrozen reset physics time //
	if(!WorldFrozen){
		if(_PhysicalWorld)
			_PhysicalWorld->ClearTimers();
	}
}

DLLEXPORT RayCastHitEntity* Leviathan::GameWorld::CastRayGetFirstHit(const Float3 &from, const Float3 &to, ObjectLock &guard){
	VerifyLock(guard);
	// Create a data object for the ray cast //
	RayCastData data(1, from, to);

	// Call the actual ray firing function //
	NewtonWorldRayCast(_PhysicalWorld->GetNewtonWorld(), &from.X, &to.X, RayCallbackDataCallbackClosest, &data, NULL, 0);

	// Check the result //
	if(data.HitEntities.size() == 0){
		// Nothing hit //
		return new RayCastHitEntity();
	}
	// We need to increase reference count to not to accidentally delete the result while caller is using it //
	data.HitEntities[0]->AddRef();
	// Return the only hit //
	return data.HitEntities[0];
}
// \todo improve this performance //
dFloat Leviathan::GameWorld::RayCallbackDataCallbackClosest(const NewtonBody* const body, const NewtonCollision* const shapeHit, const dFloat* const hitContact, const dFloat* const hitNormal, dLong collisionID, void* const userData, dFloat intersectParam){
	// Let's just store it as NewtonBody pointer //
	RayCastData* data = reinterpret_cast<RayCastData*>(userData);

	if(data->HitEntities.size() == 0)
		data->HitEntities.push_back(new RayCastHitEntity(body, intersectParam, data));
	else
		*data->HitEntities[0] = RayCastHitEntity(body, intersectParam, data);

	// Continue //
	return intersectParam;
}

DLLEXPORT RayCastHitEntity* Leviathan::GameWorld::CastRayGetFirstHitProxy(Float3 from, Float3 to){
	return CastRayGetFirstHit(from, to);
}

DLLEXPORT void Leviathan::GameWorld::MarkForClear(){
	ClearAllObjects = true;
}
// ------------------ RayCastHitEntity ------------------ //
DLLEXPORT Leviathan::RayCastHitEntity::RayCastHitEntity(const NewtonBody* ptr /*= NULL*/, const float &tvar, RayCastData* ownerptr) : HitEntity(ptr), 
	HitVariable(tvar)
{
	if(ownerptr){
		HitLocation = ownerptr->BaseHitLocationCalcVar*HitVariable;
	} else {
		HitLocation = (Float3)0;
	}
}

DLLEXPORT bool Leviathan::RayCastHitEntity::HasHit(){
	return HitEntity != NULL;
}

DLLEXPORT bool Leviathan::RayCastHitEntity::DoesBodyMatchThisHit(NewtonBody* other){
	return HitEntity == other;
}

DLLEXPORT Float3 Leviathan::RayCastHitEntity::GetPosition(){
	return HitLocation;
}

DLLEXPORT RayCastHitEntity* Leviathan::RayCastHitEntity::operator=(const RayCastHitEntity& other){
	HitEntity = other.HitEntity;
	HitVariable = other.HitVariable;
	HitLocation = other.HitLocation;

	return this;
}
// ------------------ RayCastData ------------------ //
DLLEXPORT Leviathan::RayCastData::RayCastData(int maxcount, const Float3 &from, const Float3 &to) : MaxCount(maxcount), 
	// Formula based on helpful guy on Newton forums
	BaseHitLocationCalcVar(from+(to-from))
{
	// Reserve memory for maximum number of objects //
	HitEntities.reserve(maxcount);
}

DLLEXPORT Leviathan::RayCastData::~RayCastData(){
	// We want to release all hit data //
	SAFE_RELEASE_VECTOR(HitEntities);
}