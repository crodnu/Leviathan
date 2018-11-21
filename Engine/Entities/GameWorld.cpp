// ------------------------------------ //
#include "GameWorld.h"

#include "ScriptComponentHolder.h"
#include "ScriptSystemWrapper.h"

#include "Common/DataStoring/NamedVars.h"
#include "Engine.h"
#include "Handlers/IDFactory.h"
#include "Networking/Connection.h"
#include "Networking/NetworkHandler.h"
#include "Networking/NetworkRequest.h"
#include "Networking/NetworkResponse.h"
#include "Networking/NetworkServerInterface.h"
#include "Physics/PhysicalWorld.h"
#include "Physics/PhysicsMaterialManager.h"
#include "Script/ScriptConversionHelpers.h"
#include "Script/ScriptExecutor.h"
#include "Serializers/EntitySerializer.h"
#include "Threading/ThreadingManager.h"
#include "Window.h"

// Camera interpolation
#include "Generated/ComponentStates.h"
#include "StateInterpolator.h"

#include "Sound/SoundDevice.h"

#include "Compositor/OgreCompositorManager2.h"
#include "Compositor/OgreCompositorWorkspace.h"
#include "OgreCamera.h"
#include "OgreLight.h"
#include "OgreRenderWindow.h"
#include "OgreRoot.h"
#include "OgreSceneManager.h"
#include "OgreSceneNode.h"
#include "OgreViewport.h"

#include "Exceptions.h"

#include "add_on/scriptarray/scriptarray.h"

using namespace Leviathan;
// ------------------------------------ //

// // Ray callbacks //
// static dFloat RayCallbackDataCallbackClosest(const NewtonBody* const body,
//     const NewtonCollision* const shapeHit, const dFloat* const hitContact,
//     const dFloat* const hitNormal, dLong collisionID, void* const userData,
//     dFloat intersectParam);

// ------------------------------------ //
class GameWorld::Implementation {
public:
    ~Implementation()
    {
        // Check that there are no external references to these
        for(const auto& tuple : RegisteredScriptComponents) {

            if(tuple.second->GetRefCount() != 1) {

                LOG_FATAL("GameWorld: ImplRelease: RegisteredScriptComponent (holder): has "
                          "external refs, count: " +
                          std::to_string(tuple.second->GetRefCount()));
            }
        }
    }

    std::map<std::string, ScriptComponentHolder::pointer> RegisteredScriptComponents;
    std::map<std::string, std::unique_ptr<ScriptSystemWrapper>> RegisteredScriptSystems;
};

// ------------------------------------ //
DLLEXPORT GameWorld::GameWorld(
    int32_t worldtype, const std::shared_ptr<PhysicsMaterialManager>& physicsMaterials) :
    pimpl(std::make_unique<Implementation>()),
    PhysicsMaterials(physicsMaterials), ID(IDFactory::GetID()), WorldType(worldtype)
{}

DLLEXPORT GameWorld::~GameWorld()
{
    (*WorldDestroyed) = true;

    // Assert if all objects haven't been released already.
    // We can't call virtual methods here anymore
    // This can be hit in tests quite easily if something throws an exception
    LEVIATHAN_ASSERT(
        Entities.empty(), "GameWorld: Entities not empty in destructor. Was Release called?");
}
// ------------------------------------ //
DLLEXPORT bool GameWorld::Init(const WorldNetworkSettings& network, Ogre::Root* ogre)
{
    NetworkSettings = network;

    // Detecting non-GUI mode //
    if(ogre) {

        if(!ogre)
            return false;

        GraphicalMode = true;
        // these are always required for worlds //
        _CreateOgreResources(ogre);
    }

    // Acquire physics engine world only if we have been given physical materials indicating
    // that physics is wanted
    if(PhysicsMaterials) {

        _PhysicalWorld = std::make_shared<PhysicalWorld>(this, PhysicsMaterials.get());
    }

    _DoSystemsInit();
    return true;
}

DLLEXPORT void GameWorld::Release()
{
    _DoSystemsRelease();

    (*WorldDestroyed) = true;

    ReceivingPlayers.clear();

    // release objects //
    // TODO: allow objects to know that they are about to get killed

    // As all objects are just pointers to components we can just dump the objects
    // and once the component pools are released
    ClearEntities();

    if(GraphicalMode) {
        // TODO: notify our window that it no longer has a world workspace
        LinkedToWindow = nullptr;

        // release Ogre resources //

        // Destroy the compositor //
        Ogre::Root& ogre = Ogre::Root::getSingleton();

        // Allow releasing twice
        if(WorldWorkspace) {
            ogre.getCompositorManager2()->removeWorkspace(WorldWorkspace);
            WorldWorkspace = nullptr;
        }

        if(WorldsScene) {
            ogre.destroySceneManager(WorldsScene);
            WorldsScene = nullptr;
        }
    }

    // This should be relatively cheap if the newton threads don't deadlock while waiting
    // for each other
    _PhysicalWorld.reset();

    // Let go of our these resources
    pimpl.reset();
}
// ------------------------------------ //
void GameWorld::_CreateOgreResources(Ogre::Root* ogre)
{
    // create scene manager //
    // Let's do what the Ogre samples do and use a bunch of threads for culling
    const auto threads = std::max(2, static_cast<int>(std::thread::hardware_concurrency()));

    // TODO: allow configuring scene type (the type was: Ogre::ST_EXTERIOR_FAR before)
    WorldsScene = ogre->createSceneManager(Ogre::ST_GENERIC, threads,
        Ogre::INSTANCING_CULLING_THREADED, "MainSceneManager_" + Convert::ToString(ID));

    // These are a bit higher than the Ogre "sane" values of 500.0f
    WorldsScene->setShadowDirectionalLightExtrusionDistance(1000.f);
    WorldsScene->setShadowFarDistance(1000.f);

    // Setup v2 rendering for the default group
    WorldsScene->getRenderQueue()->setRenderQueueMode(
        DEFAULT_RENDER_QUEUE, Ogre::RenderQueue::FAST);

    // create camera //
    WorldSceneCamera = WorldsScene->createCamera("Camera01");

    // WorldSceneCamera->lookAt( Ogre::Vector3( 0, 0, 0 ) );

    // near and far clipping planes //
    WorldSceneCamera->setFOVy(Ogre::Degree(60));
    WorldSceneCamera->setNearClipDistance(0.1f);
    WorldSceneCamera->setFarClipDistance(50000.f);
    WorldSceneCamera->setAutoAspectRatio(true);

    // enable infinite far clip distance if supported //
    if(ogre->getRenderSystem()->getCapabilities()->hasCapability(
           Ogre::RSC_INFINITE_FAR_PLANE)) {

        WorldSceneCamera->setFarClipDistance(0);
    }

    // // Orthographic test
    // WorldSceneCamera->setProjectionType(Ogre::PT_ORTHOGRAPHIC);
    // WorldSceneCamera->setOrthoWindow(85, 85);

    // default sun //
    SetSunlight();
}
// ------------------------------------ //
DLLEXPORT void GameWorld::SetFog()
{
    WorldsScene->setFog(Ogre::FOG_LINEAR, Ogre::ColourValue(0.7f, 0.7f, 0.8f), 0, 4000, 10000);
    WorldsScene->setFog(Ogre::FOG_NONE);
}

DLLEXPORT void GameWorld::SetSunlight()
{
    // create/update things if they are nullptr //
    if(!Sunlight) {
        Sunlight = WorldsScene->createLight();
        Sunlight->setName("sunlight");
    }

    if(!SunLightNode) {

        SunLightNode = WorldsScene->getRootSceneNode()->createChildSceneNode();
        SunLightNode->setName("sunlight node");

        SunLightNode->attachObject(Sunlight);
    }

    Sunlight->setType(Ogre::Light::LT_DIRECTIONAL);
    // Sunlight->setDiffuseColour(0.98f, 1.f, 0.95f);
    Sunlight->setDiffuseColour(1.f, 1.f, 1.f);
    Sunlight->setSpecularColour(1.f, 1.f, 1.f);
    // Sunlight->setDirection(Float3(-1, -1, -1).Normalize());
    Sunlight->setDirection(Float3(0.55f, -0.3f, 0.75f).Normalize());
    Sunlight->setPowerScale(7.0f);

    // Set scene ambient colour //
    // TODO: Ogre samples also use this so maybe this works with PBR HLMS system
    // WorldsScene->setAmbientLight(Ogre::ColourValue(0.3f, 0.5f, 0.7f) * 0.1f * 0.75f,
    //     Ogre::ColourValue(0.6f, 0.45f, 0.3f) * 0.065f * 0.75f,
    //     -Sunlight->getDirection() + Ogre::Vector3::UNIT_Y * 0.2f);
    WorldsScene->setAmbientLight(Ogre::ColourValue(0.3f, 0.3f, 0.3f),
        Ogre::ColourValue(0.2f, 0.2f, 0.2f), /*Ogre::Vector3(0.1f, 1.f, 0.085f),*/
        -Sunlight->getDirection() + Ogre::Vector3::UNIT_Y * 0.2f);
}

DLLEXPORT void GameWorld::RemoveSunlight()
{
    if(SunLightNode) {
        SunLightNode->detachAllObjects();
        // might be safe to delete
        OGRE_DELETE SunLightNode;
        SunLightNode = nullptr;
    }
}

DLLEXPORT void GameWorld::SetLightProperties(const Ogre::ColourValue& diffuse,
    const Ogre::ColourValue& specular, const Ogre::Vector3& direction, float power,
    const Ogre::ColourValue& upperhemisphere, const Ogre::ColourValue& lowerhemisphere,
    const Ogre::Vector3& hemispheredir, float envmapscale /*= 1.0f*/)
{
    if(!Sunlight || !SunLightNode) {

        LOG_ERROR("GameWorld: SetLightProperties: world doesn't have sun light set");
        return;
    }

    Sunlight->setDiffuseColour(diffuse);
    Sunlight->setSpecularColour(specular);
    Sunlight->setDirection(direction);
    Sunlight->setPowerScale(power);

    WorldsScene->setAmbientLight(upperhemisphere, lowerhemisphere, hemispheredir, envmapscale);
}

// ------------------------------------ //
DLLEXPORT void GameWorld::Render(int mspassed, int tick, int timeintick)
{
    if(InBackground) {

        LOG_ERROR("GameWorld: Render: called while world is in the background (not attached "
                  "to a window)");
        return;
    }

    RunFrameRenderSystems(tick, timeintick);

    // Read camera entity and update position //

    // Skip if no camera //
    if(CameraEntity == 0)
        return;

    try {
        Camera& properties = GetComponent<Camera>(CameraEntity);

        Position& position = GetComponent<Position>(CameraEntity);

        auto& states = GetStatesFor<Position>();

        // set camera position //
        const auto interpolated =
            StateInterpolator::Interpolate(states, CameraEntity, &position, tick, timeintick);

        if(!std::get<0>(interpolated)) {

            // No interpolated pos //
            WorldSceneCamera->setPosition(position.Members._Position);
            WorldSceneCamera->setOrientation(position.Members._Orientation);

        } else {

            const auto& interpolatedPos = std::get<1>(interpolated);
            WorldSceneCamera->setPosition(interpolatedPos._Position);
            WorldSceneCamera->setOrientation(interpolatedPos._Orientation);
        }

        if(properties.SoundPerceiver) {

            Engine::Get()->GetSoundDevice()->SetSoundListenerPosition(
                position.Members._Position, position.Members._Orientation);
        }

        if(properties.Marked || AppliedCameraPropertiesPtr != &properties) {

            AppliedCameraPropertiesPtr = &properties;

            WorldSceneCamera->setFOVy(Ogre::Degree(properties.FOVY));

            properties.Marked = false;
        }

    } catch(const Exception& e) {

        LOG_ERROR("GameWorld: Render: camera update failed. Was a component removed?, "
                  "exception:");
        e.PrintToLog();
        CameraEntity = 0;
        return;
    }
}
// ------------------------------------ //
DLLEXPORT void GameWorld::SetCamera(ObjectID object)
{
    CameraEntity = object;

    AppliedCameraPropertiesPtr = nullptr;

    if(CameraEntity == NULL_OBJECT)
        return;

    // Check components //
    try {
        GetComponent<Camera>(object);
    } catch(const NotFound&) {

        throw InvalidArgument("SetCamera object is missing a needed component (Camera)");
    }

    try {
        GetComponent<Position>(object);
    } catch(const NotFound&) {

        throw InvalidArgument("SetCamera object is missing a needed component (Position)");
    }
}

DLLEXPORT Ogre::Ray GameWorld::CastRayFromCamera(float x, float y) const
{
    // Fail if there is no active camera //
    if(CameraEntity == NULL_OBJECT)
        throw InvalidState("This world has no active CameraEntity");

    if(!WorldSceneCamera)
        throw InvalidState("This world has initialized Ogre resources");

    // Read the latest set data from the camera
    // TODO: could jump to the actual latest position here if wanted
    return WorldSceneCamera->getCameraToViewportRay(x, y);
}
// ------------------------------------ //
DLLEXPORT bool GameWorld::ShouldPlayerReceiveEntity(
    Position& atposition, Connection& connection)
{
    return true;
}

DLLEXPORT bool GameWorld::IsConnectionInWorld(Connection& connection) const
{
    for(auto& player : ReceivingPlayers) {

        if(player->GetConnection().get() == &connection) {

            return true;
        }
    }

    return false;
}

DLLEXPORT void GameWorld::SetPlayerReceiveWorld(std::shared_ptr<ConnectedPlayer> ply)
{
    // Skip if already added //
    for(auto& player : ReceivingPlayers) {

        if(player == ply) {

            return;
        }
    }

    LOG_INFO("GameWorld: player(\"" + ply->GetNickname() + "\") is now receiving world");

    // Add them to the list of receiving players //
    ReceivingPlayers.push_back(ply);

    if(!ply->GetConnection()->IsValidForSend()) {

        // The closing should be handled by somebody else
        Logger::Get()->Error("GameWorld: requested to sync with a player who has closed their "
                             "connection");

        return;
    }

    {
        // Start world receive information
        ply->GetConnection()->SendPacketToConnection(
            std::make_shared<ResponseStartWorldReceive>(0, ID, WorldType),
            RECEIVE_GUARANTEE::Critical);
    }

    // Update the position data //
    UpdatePlayersPositionData(*ply);

    // Start sending initial update //
    Logger::Get()->Info(
        "Starting to send " + Convert::ToString(Entities.size()) + " to player");

    // Now we can queue all objects for sending //
    // TODO: make sure that all objects are sent
    // TODO: redo this inside the world tick
    // ThreadingManager::Get()->QueueTask(
    //     new RepeatCountedTask(std::bind<void>([](
    //                 std::shared_ptr<Connection> connection,
    //                 std::shared_ptr<ConnectedPlayer> processingobject, GameWorld* world,
    //                 std::shared_ptr<bool> WorldInvalid)
    //             -> void
    //     {
    //         // Get the next object //
    //         RepeatCountedTask* task =
    //             dynamic_cast<RepeatCountedTask*>(TaskThread::GetThreadSpecificThreadObject()->
    //             QuickTaskAccess.get());

    //         LEVIATHAN_ASSERT(task, "wrong type passed to our task");

    //         size_t num = task->GetRepeatCount();

    //         if(*WorldInvalid){

    //         taskstopprocessingobjectsforinitialsynclabel:

    //             // Stop processing //
    //             task->StopRepeating();
    //             return;
    //         }

    //         // Stop if out of bounds //
    //         if(num >= world->Entities.size()){

    //             goto taskstopprocessingobjectsforinitialsynclabel;
    //         }

    //         // Get the object //
    //         auto tosend = world->Entities[num];

    //         // Skip if shouldn't send //
    //         try{

    //             auto& position = world->GetComponent<Position>(tosend);

    //             if(!world->ShouldPlayerReceiveObject(position, *connection)){

    //                 return;
    //             }

    //         } catch(const NotFound&){

    //             // No position, should probably always send //
    //         }


    //         // Send it //
    //         world->SendObjectToConnection(guard, tosend, connection);

    //         return;

    //     }, ply->GetConnection(), ply, this, WorldDestroyed), Entities.size()));
}

DLLEXPORT void GameWorld::SendToAllPlayers(
    const std::shared_ptr<NetworkResponse>& response, RECEIVE_GUARANTEE guarantee) const
{
    // Notify everybody that an entity has been destroyed //
    for(auto iter = ReceivingPlayers.begin(); iter != ReceivingPlayers.end(); ++iter) {

        auto safe = (*iter)->GetConnection();

        if(!safe->IsValidForSend()) {
            // Player has probably closed their connection //
            continue;
        }

        safe->SendPacketToConnection(response, guarantee);
    }
}
// ------------------------------------ //
DLLEXPORT void GameWorld::Tick(int currenttick)
{
    if(InBackground && !TickWhileInBackground)
        return;

    TickNumber = currenttick;

    // Apply queued packets //
    ApplyQueuedPackets();

    _HandleDelayedDelete();

    // All required nodes for entities are created //
    HandleAddedAndDeleted();
    ClearAddedAndRemoved();

    // Remove closed player connections //

    for(auto iter = ReceivingPlayers.begin(); iter != ReceivingPlayers.end(); ++iter) {

        if(!(*iter)->GetConnection()->IsValidForSend()) {

            DEBUG_BREAK;

        } else {

            ++iter;
        }
    }

    // Simulate physics //
    if(!WorldFrozen) {

        // TODO: a game type that is a client and server at  the same time
        // if(IsOnServer) {

        _ApplyEntityUpdatePackets();
        if(_PhysicalWorld)
            _PhysicalWorld->SimulateWorld(TICKSPEED / 1000.f);

        // } else {

        // Simulate direct control //
        // }
    }

    TickInProgress = true;

    _RunTickSystems();

    TickInProgress = false;

    // Sendable objects may need something to be done //

    if(NetworkSettings.IsAuthoritative) {

        // Notify new entities //
        // DEBUG_BREAK;

        // Skip if not tick that will be stored //
        // if(TickNumber % WORLD_OBJECT_UPDATE_CLIENTS_INTERVAL == 0){

        //     _SendableSystem.Run(ComponentSendable.GetIndex(), *this);
        // }

    } else {

        // TODO: direct control objects
        // _ReceivedSystem.Run(ComponentReceived.GetIndex(), *this);
    }
}
// ------------------------------------ //
DLLEXPORT void GameWorld::HandleAddedAndDeleted()
{
    // We are responsible for script systems //
    for(auto iter = pimpl->RegisteredScriptSystems.begin();
        iter != pimpl->RegisteredScriptSystems.end(); ++iter) {

        iter->second->CreateAndDestroyNodes();
    }
}

DLLEXPORT void GameWorld::ClearAddedAndRemoved()
{
    // We are responsible for script components //
    for(auto iter = pimpl->RegisteredScriptComponents.begin();
        iter != pimpl->RegisteredScriptComponents.end(); ++iter) {

        iter->second->ClearAdded();
        iter->second->ClearRemoved();
    }
}
// ------------------------------------ //
DLLEXPORT void GameWorld::_ResetSystems()
{
    // Skip double Release
    if(!pimpl)
        return;

    // We are responsible for script systems //
    for(auto iter = pimpl->RegisteredScriptSystems.begin();
        iter != pimpl->RegisteredScriptSystems.end(); ++iter) {

        iter->second->Clear();
    }
}

DLLEXPORT void GameWorld::_ResetOrReleaseComponents()
{
    // Skip double Release
    if(!pimpl)
        return;

    // We are responsible for script components //
    for(auto iter = pimpl->RegisteredScriptComponents.begin();
        iter != pimpl->RegisteredScriptComponents.end(); ++iter) {

        iter->second->ReleaseAllComponents();
    }
}
// ------------------------------------ //
DLLEXPORT void GameWorld::RunFrameRenderSystems(int tick, int timeintick)
{
    // Don't have any systems, but these updates may be important for interpolation //
    _ApplyEntityUpdatePackets();

    // TODO: if there are any impactful simulation done here it needs to be also inside a block
    // where TickInProgress is set to true
}

DLLEXPORT void GameWorld::_RunTickSystems()
{
    // We are responsible for script systems //
    for(auto iter = pimpl->RegisteredScriptSystems.begin();
        iter != pimpl->RegisteredScriptSystems.end(); ++iter) {

        iter->second->Run();
    }
}
// ------------------------------------ //
DLLEXPORT int GameWorld::GetTickNumber() const
{
    return TickNumber;
}

DLLEXPORT float GameWorld::GetTickProgress() const
{
    float progress = Engine::Get()->GetTimeSinceLastTick() / (float)TICKSPEED;

    if(progress < 0.f)
        return 0.f;

    return progress < 1.f ? progress : 1.f;
}

DLLEXPORT std::tuple<int, int> GameWorld::GetTickAndTime() const
{
    int tick = TickNumber;
    int timeSince = static_cast<int>(Engine::Get()->GetTimeSinceLastTick());

    while(timeSince >= TICKSPEED) {

        ++tick;
        timeSince -= TICKSPEED;
    }

    return std::make_tuple(tick, timeSince);
}

// ------------------ Object managing ------------------ //
DLLEXPORT ObjectID GameWorld::CreateEntity()
{
    auto id = static_cast<ObjectID>(IDFactory::GetID());

    Entities.push_back(id);

    return id;
}

DLLEXPORT void GameWorld::NotifyEntityCreate(ObjectID id)
{
    if(NetworkSettings.IsAuthoritative) {

        // This is at least a decent place to send them,
        Sendable* issendable = nullptr;

        try {
            issendable = &GetComponent<Sendable>(id);

        } catch(const NotFound&) {

            // Not sendable no point in continuing //
            return;
        }

        LEVIATHAN_ASSERT(issendable, "GetComponent didn't throw");

        auto end = ReceivingPlayers.end();
        for(auto iter = ReceivingPlayers.begin(); iter != end; ++iter) {

            auto safe = (*iter)->GetConnection();

            if(!safe->IsValidForSend()) {
                // Player has probably closed their connection //
                continue;
            }

            // TODO: pass issendable here to avoid an extra lookup
            if(!SendEntityToConnection(id, safe)) {

                Logger::Get()->Warning("GameWorld: CreateEntity: failed to send "
                                       "object to player (" +
                                       (*iter)->GetNickname() + ")");

                continue;
            }
        }

    } else {

        // Clients register received objects here //
        Entities.push_back(id);
    }
}
// ------------------------------------ //
DLLEXPORT void GameWorld::ClearEntities()
{
    // Release objects //
    Entities.clear();
    Parents.clear();
    // This shouldn't be used all that much so release the memory
    Parents.shrink_to_fit();

    // Clear all nodes //
    _ResetSystems();

    // Clears all components
    // Runs Release on components that need it
    _ResetOrReleaseComponents();

    // Notify everybody that all entities are discarded //
    for(auto iter = ReceivingPlayers.begin(); iter != ReceivingPlayers.end(); ++iter) {

        auto safe = (*iter)->GetConnection();

        if(!safe->IsValidForSend()) {
            // Player has probably closed their connection //
            continue;
        }

        Logger::Get()->Write("TODO: send world clear message");
        DEBUG_BREAK;
    }
}
// ------------------------------------ //
// DLLEXPORT Float3 GameWorld::GetGravityAtPosition(const Float3& pos)
// {
//     // \todo take position into account //
//     // create force without mass applied //
//     return Float3(0.f, PHYSICS_BASE_GRAVITY, 0.f);
// }
// ------------------------------------ //
DLLEXPORT int GameWorld::GetPhysicalMaterial(const std::string& name)
{
    if(!PhysicsMaterials)
        return -1;

    return PhysicsMaterials->GetMaterialID(name);
}
// ------------------------------------ //
DLLEXPORT void GameWorld::DestroyEntity(ObjectID id)
{
    // Fail if ticking currently //
    if(TickInProgress)
        throw InvalidState(
            "Cannot DestroyEntity while ticking. Use QueueDestroyEntity instead");

    auto end = Entities.end();
    for(auto iter = Entities.begin(); iter != end; ++iter) {

        if(*iter == id) {

            Entities.erase(iter);
            _DoDestroy(id);
            return;
        }
    }
}

DLLEXPORT void GameWorld::QueueDestroyEntity(ObjectID id)
{
    Lock lock(DeleteMutex);
    DelayedDeleteIDS.push_back(id);
}

void GameWorld::_HandleDelayedDelete()
{

    // We might want to delete everything //
    if(ClearAllEntities) {

        ClearEntities();

        ClearAllEntities = false;

        Lock lock(DeleteMutex);
        DelayedDeleteIDS.clear();

        // All are now cleared //
        return;
    }

    Lock lock(DeleteMutex);

    // Return right away if no objects to delete //
    if(DelayedDeleteIDS.empty())
        return;

    // Search all objects and find the ones that need to be deleted //
    for(auto iter = Entities.begin(); iter != Entities.end();) {

        // Check does id match any //
        auto curid = *iter;
        bool delthis = false;

        for(auto iterids = DelayedDeleteIDS.begin(); iterids != DelayedDeleteIDS.end();) {

            if(*iterids == curid) {
                // Remove this as it will get deleted //
                delthis = true;
                DelayedDeleteIDS.erase(iterids);
                break;

            } else {
                ++iterids;
            }
        }

        if(delthis) {

            _DoDestroy(curid);
            iter = Entities.erase(iter);

            // Check for end //
            if(DelayedDeleteIDS.empty())
                return;

        } else {
            ++iter;
        }
    }
}

void GameWorld::_DoDestroy(ObjectID id)
{
    // LOG_INFO("GameWorld destroying object " + Convert::ToString(id));

    if(NetworkSettings.IsAuthoritative)
        _ReportEntityDestruction(id);

    // TODO: find a better way to do this
    DestroyAllIn(id);

    // Parent destroy children //
    // We need to support recursively parented entities
    for(size_t i = 0; i < Parents.size();) {

        if(std::get<0>(Parents[i]) == id) {

            const auto childId = std::get<1>(Parents[i]);

            // Remove it //
            std::swap(Parents[i], Parents[Parents.size() - 1]);
            Parents.pop_back();

            // And then destroy //
            // LOG_WRITE("Destroying child ID: " + std::to_string(childId));
            DestroyEntity(childId);

            // To support recursively parented we go back to the start to scan again
            i = 0;

        } else if(std::get<1>(Parents[i]) == id) {
            // Child has been destroyed //
            // Remove it //
            std::swap(Parents[i], Parents[Parents.size() - 1]);
            Parents.pop_back();

        } else {
            ++i;
        }
    }
}
// ------------------------------------ //
DLLEXPORT void GameWorld::SetEntitysParent(ObjectID child, ObjectID parent)
{
    Parents.push_back(std::make_tuple(parent, child));
}
// ------------------------------------ //
DLLEXPORT std::tuple<void*, bool> GameWorld::GetComponent(ObjectID id, COMPONENT_TYPE type)
{
    return std::make_tuple(nullptr, false);
}

DLLEXPORT std::tuple<void*, ComponentTypeInfo, bool> GameWorld::GetComponentWithType(
    ObjectID id, COMPONENT_TYPE type)
{
    return std::make_tuple(nullptr, ComponentTypeInfo(-1, -1), false);
}

DLLEXPORT std::tuple<void*, bool> GameWorld::GetStatesFor(COMPONENT_TYPE type)
{
    return std::make_tuple(nullptr, false);
}

DLLEXPORT bool GameWorld::GetRemovedFor(
    COMPONENT_TYPE type, std::vector<std::tuple<void*, ObjectID>>& result)
{
    return false;
}

DLLEXPORT bool GameWorld::GetRemovedForScriptDefined(
    const std::string& name, std::vector<std::tuple<asIScriptObject*, ObjectID>>& result)
{
    auto iter = pimpl->RegisteredScriptComponents.find(name);

    if(iter == pimpl->RegisteredScriptComponents.end())
        return false;

    auto& removed = iter->second->GetRemoved();

    result.insert(std::end(result), std::begin(removed), std::end(removed));
    return true;
}

DLLEXPORT bool GameWorld::GetAddedFor(
    COMPONENT_TYPE type, std::vector<std::tuple<void*, ObjectID, ComponentTypeInfo>>& result)
{
    return false;
}

DLLEXPORT bool GameWorld::GetAddedForScriptDefined(const std::string& name,
    std::vector<std::tuple<asIScriptObject*, ObjectID, ScriptComponentHolder*>>& result)
{
    auto iter = pimpl->RegisteredScriptComponents.find(name);

    if(iter == pimpl->RegisteredScriptComponents.end())
        return false;

    auto& added = iter->second->GetAdded();

    result.reserve(result.size() + added.size());

    for(const auto& tuple : added) {

        result.push_back(
            std::make_tuple(std::get<0>(tuple), std::get<1>(tuple), iter->second.get()));
    }

    return true;
}

// ------------------------------------ //
DLLEXPORT void GameWorld::_DoSystemsInit()
{
    // Script systems are initialized as they are created
}

DLLEXPORT void GameWorld::_DoSystemsRelease()
{
    // This can be called after Releasing once already
    if(!pimpl)
        return;

    // We are responsible for script systems //
    for(auto iter = pimpl->RegisteredScriptSystems.begin();
        iter != pimpl->RegisteredScriptSystems.end(); ++iter) {

        iter->second->Release();
    }

    pimpl->RegisteredScriptSystems.clear();
}

DLLEXPORT void GameWorld::_DoSuspendSystems()
{
    // We are responsible for script systems //
    for(auto iter = pimpl->RegisteredScriptSystems.begin();
        iter != pimpl->RegisteredScriptSystems.end(); ++iter) {

        iter->second->Suspend();
    }
}

DLLEXPORT void GameWorld::_DoResumeSystems()
{
    // We are responsible for script systems //
    for(auto iter = pimpl->RegisteredScriptSystems.begin();
        iter != pimpl->RegisteredScriptSystems.end(); ++iter) {

        iter->second->Resume();
    }
}

// ------------------------------------ //
DLLEXPORT void GameWorld::DestroyAllIn(ObjectID id)
{
    // This can be called after Releasing once already
    if(!pimpl)
        return;

    // Handle script types
    for(auto iter = pimpl->RegisteredScriptComponents.begin();
        iter != pimpl->RegisteredScriptComponents.end(); ++iter) {

        // Just try to remove like the normal c++ components until there is a better way
        iter->second->ReleaseComponent(id);
    }
}
// ------------------------------------ //
void GameWorld::_ReportEntityDestruction(ObjectID id)
{
    SendToAllPlayers(std::make_shared<ResponseEntityDestruction>(0, this->ID, id),
        RECEIVE_GUARANTEE::Critical);
}

// ------------------------------------ //
DLLEXPORT void GameWorld::SetWorldPhysicsFrozenState(bool frozen)
{
    // Skip if set to the same //
    if(frozen == WorldFrozen)
        return;

    WorldFrozen = frozen;

    // Send it to receiving players (if we are a server) //
    if(ReceivingPlayers.empty())
        return;

    // Should be safe to create the packet now and send it to all the connections //
    SendToAllPlayers(std::make_shared<ResponseWorldFrozen>(0, ID, WorldFrozen, TickNumber),
        RECEIVE_GUARANTEE::Critical);
}

// DLLEXPORT RayCastHitEntity* GameWorld::CastRayGetFirstHit(const Float3& from, const Float3&
// to)
// {
//     // Create a data object for the ray cast //
//     RayCastData data(1, from, to);

//     // Call the actual ray firing function //
//     NewtonWorldRayCast(_PhysicalWorld->GetNewtonWorld(), &from.X, &to.X,
//         RayCallbackDataCallbackClosest, &data, nullptr, 0);

//     // Check the result //
//     if(data.HitEntities.size() == 0) {
//         // Nothing hit //
//         return new RayCastHitEntity();
//     }

//     // We need to increase reference count to not to accidentally delete the result while
//     // caller is using it
//     data.HitEntities[0]->AddRef();

//     // Return the only hit //
//     return data.HitEntities[0];
// }


// //! \todo improve this performance //
// dFloat RayCallbackDataCallbackClosest(const NewtonBody* const body,
//     const NewtonCollision* const shapeHit, const dFloat* const hitContact,
//     const dFloat* const hitNormal, dLong collisionID, void* const userData,
//     dFloat intersectParam)
// {
//     // Let's just store it as a NewtonBody pointer //
//     RayCastData* data = reinterpret_cast<RayCastData*>(userData);

//     if(data->HitEntities.size() == 0)
//         data->HitEntities.push_back(new RayCastHitEntity(body, intersectParam, data));
//     else
//         *data->HitEntities[0] = RayCastHitEntity(body, intersectParam, data);

//     // Continue //
//     return intersectParam;
// }

DLLEXPORT void GameWorld::MarkForClear()
{
    ClearAllEntities = true;
}
// ------------------------------------ //
void GameWorld::UpdatePlayersPositionData(ConnectedPlayer& ply)
{
    // Get the position for this player in this world //
    ObjectID id = ply.GetPositionInWorld(this);

    // Player is using a static position at (0, 0, 0) //
    if(id == 0)
        return;

    try {

        auto& position = GetComponent<Position>(id);

        (void)position.Members._Position;

    } catch(const NotFound&) {

        // Player has invalid position //
        Logger::Get()->Warning("Player position entity has no Position component");
    }
}
// ------------------------------------ //
DLLEXPORT bool GameWorld::SendEntityToConnection(
    ObjectID id, std::shared_ptr<Connection> connection)
{
    // First create a packet which will be the object's data //

    sf::Packet packet;

    DEBUG_BREAK;
    // try {
    //     if (!Engine::Get()->GetEntitySerializer()->CreatePacketForConnection(this, id,
    //         GetComponent<Sendable>(id), packet, *connection))
    //     {
    //         return false;
    //     }
    // }
    // catch (const NotFound&) {
    //     return false;
    // }

    // Then gather all sorts of other stuff to make an response //
    return connection
                   ->SendPacketToConnection(
                       std::make_shared<ResponseEntityCreation>(0, id, std::move(packet)),
                       RECEIVE_GUARANTEE::Critical)
                   .get() ?
               true :
               false;
}
// ------------------------------------ //
DLLEXPORT void GameWorld::HandleEntityInitialPacket(
    std::shared_ptr<NetworkResponse> message, ResponseEntityCreation* data)
{
    if(!data)
        return;

    InitialEntityPackets.push_back(message);
}

void GameWorld::_ApplyInitialEntityPackets()
{
    auto serializer = Engine::Get()->GetEntitySerializer();

    for(auto& response : InitialEntityPackets) {

        LEVIATHAN_ASSERT(response->GetType() == NETWORK_RESPONSE_TYPE::EntityCreation,
            "invalid type in InitialEntityPackets");

        ObjectID id = 0;

        DEBUG_BREAK;
        // serializer->DeserializeWholeEntityFromPacket(this, id,
        //     static_cast<ResponseEntityCreation*>(response.get())->InitialEntity);

        if(id < 1) {

            Logger::Get()->Error("GameWorld: handle initial packet: failed to create entity");
        }
    }

    InitialEntityPackets.clear();
}

DLLEXPORT void GameWorld::HandleEntityUpdatePacket(std::shared_ptr<NetworkResponse> message)
{
    if(message->GetType() == NETWORK_RESPONSE_TYPE::EntityUpdate)
        return;

    EntityUpdatePackets.push_back(message);
}

void GameWorld::_ApplyEntityUpdatePackets()
{
    auto serializer = Engine::Get()->GetEntitySerializer();

    for(auto& response : EntityUpdatePackets) {

        // Data cannot be nullptr here //
        ResponseEntityUpdate* data = static_cast<ResponseEntityUpdate*>(response.get());

        bool found = false;

        // Just check if the entity is created/exists //
        for(auto iter = Entities.begin(); iter != Entities.end(); ++iter) {

            if((*iter) == data->EntityID) {

                found = true;

                // Apply the update //
                DEBUG_BREAK;
                // if(!serializer->ApplyUpdateFromPacket(this, guard, data->EntityID,
                //         data->TickNumber, data->ReferenceTick, data->UpdateData))
                // {
                //     Logger::Get()->Warning("GameWorld("+Convert::ToString(ID)+"): "
                //         "applying update to entity " +
                //         Convert::ToString(data->EntityID) + " failed");
                // }

                break;
            }
        }

        if(!found) {
            // It hasn't been created yet //
            Logger::Get()->Warning("GameWorld(" + Convert::ToString(ID) + "): has no entity " +
                                   Convert::ToString(data->EntityID) +
                                   ", ignoring an update packet");
        }
    }

    EntityUpdatePackets.clear();
}
// ------------------------------------ //
DLLEXPORT void GameWorld::ApplyQueuedPackets()
{
    if(!InitialEntityPackets.empty())
        _ApplyInitialEntityPackets();

    if(!EntityUpdatePackets.empty())
        _ApplyEntityUpdatePackets();
}
// ------------------------------------ //
DLLEXPORT void GameWorld::HandleClockSyncPacket(RequestWorldClockSync* data)
{
    Logger::Get()->Info(
        "GameWorld: adjusting our clock: Absolute: " + Convert::ToString(data->Absolute) +
        ", tick: " + Convert::ToString(data->Ticks) +
        ", engine ms: " + Convert::ToString(data->EngineMSTweak));

    // Change our TickNumber to match //
    Engine::Get()->_AdjustTickNumber(data->Ticks, data->Absolute);

    if(data->EngineMSTweak)
        Engine::Get()->_AdjustTickClock(data->EngineMSTweak, data->Absolute);
}
// ------------------------------------ //
DLLEXPORT void GameWorld::HandleWorldFrozenPacket(ResponseWorldFrozen* data)
{
    Logger::Get()->Info(
        "GameWorld(" + Convert::ToString(ID) +
        "): frozen state updated, now: " + Convert::ToString<int>(data->Frozen) +
        ", tick: " + Convert::ToString(data->TickNumber) +
        " (our tick:" + Convert::ToString(TickNumber) + ")");

    if(data->TickNumber > TickNumber) {

        Logger::Get()->Write("TODO: freeze the world in the future");
    }

    // Set the state //
    SetWorldPhysicsFrozenState(data->Frozen);

    // Simulate ticks if required //
    if(!data->Frozen) {

        // Check how many ticks we are behind and simulate that number of physical updates //
        int tickstosimulate = TickNumber - data->TickNumber;

        if(tickstosimulate > 0) {

            Logger::Get()->Info("GameWorld: unfreezing and simulating " +
                                Convert::ToString(tickstosimulate * TICKSPEED) +
                                " ms worth of more physical updates");

            DEBUG_BREAK;
            // _PhysicalWorld->AdjustClock(tickstosimulate * TICKSPEED);
        }


    } else {

        // Snap objects back //
        Logger::Get()->Info("TODO: world freeze snap things back a bit");
    }
}
// ------------------------------------ //
DLLEXPORT CScriptArray* GameWorld::GetRemovedIDsForComponents(CScriptArray* componenttypes)
{
    if(!componenttypes)
        return nullptr;

    if(componenttypes->GetElementTypeId() !=
        AngelScriptTypeIDResolver<uint16_t>::Get(ScriptExecutor::Get())) {

        LOG_ERROR("GameWorld: GetRemovedIDsForComponents: given an array of wrong type");
        componenttypes->Release();
        return nullptr;
    }

    std::vector<std::tuple<void*, ObjectID>> result;

    for(asUINT i = 0; i < componenttypes->GetSize(); ++i) {

        uint16_t* type = static_cast<uint16_t*>(componenttypes->At(i));

        if(!GetRemovedFor(static_cast<COMPONENT_TYPE>(*type), result)) {

            LOG_WARNING("GameWorld: GetRemovedIDsForComponents: unknown component type: " +
                        std::to_string(*type));
        }
    }

    componenttypes->Release();

    asITypeInfo* typeInfo =
        ScriptExecutor::Get()->GetASEngine()->GetTypeInfoByDecl("array<ObjectID>");

    CScriptArray* array = CScriptArray::Create(typeInfo, static_cast<asUINT>(result.size()));

    if(!array)
        return nullptr;

    for(asUINT i = 0; i < static_cast<asUINT>(result.size()); ++i) {

        array->SetValue(i, &std::get<1>(result[i]));
    }

    return array;
}

DLLEXPORT CScriptArray* GameWorld::GetRemovedIDsForScriptComponents(CScriptArray* typenames)
{
    if(!typenames)
        return nullptr;

    if(typenames->GetElementTypeId() !=
        AngelScriptTypeIDResolver<std::string>::Get(ScriptExecutor::Get())) {

        LOG_ERROR("GameWorld: GetRemovedIDsForScriptComponents: given an array of wrong type "
                  "(expected array<string>)");
        typenames->Release();
        return nullptr;
    }

    std::vector<std::tuple<asIScriptObject*, ObjectID>> result;

    for(asUINT i = 0; i < typenames->GetSize(); ++i) {

        std::string* type = static_cast<std::string*>(typenames->At(i));

        if(!GetRemovedForScriptDefined(*type, result)) {

            LOG_WARNING(
                "GameWorld: GetRemovedIDsForScriptComponents: unknown component type: " +
                *type);
        }
    }

    typenames->Release();

    asITypeInfo* typeInfo =
        ScriptExecutor::Get()->GetASEngine()->GetTypeInfoByDecl("array<ObjectID>");

    CScriptArray* array = CScriptArray::Create(typeInfo, static_cast<asUINT>(result.size()));

    if(!array)
        return nullptr;

    for(asUINT i = 0; i < static_cast<asUINT>(result.size()); ++i) {

        array->SetValue(i, &std::get<1>(result[i]));
    }

    return array;
}

DLLEXPORT bool GameWorld::RegisterScriptComponentType(
    const std::string& name, asIScriptFunction* factory)
{
    // Skip if already registered //
    if(pimpl->RegisteredScriptComponents.find(name) !=
        pimpl->RegisteredScriptComponents.end()) {
        factory->Release();
        return false;
    }

    // ScriptComponentHolder takes care of releasing the reference
    pimpl->RegisteredScriptComponents[name] =
        ScriptComponentHolder::MakeShared<ScriptComponentHolder>(name, factory, this);

    return true;
}

DLLEXPORT ScriptComponentHolder* GameWorld::GetScriptComponentHolder(const std::string& name)
{
    // if called after release
    if(!pimpl)
        return nullptr;

    auto iter = pimpl->RegisteredScriptComponents.find(name);

    if(iter == pimpl->RegisteredScriptComponents.end())
        return nullptr;

    iter->second->AddRef();
    return iter->second.get();
}

DLLEXPORT bool GameWorld::RegisterScriptSystem(
    const std::string& name, asIScriptObject* system)
{
    if(!system) {
        LOG_ERROR("GameWorld: RegisterScriptSystem: passed null object as new system");
        return false;
    }

    // if called after release
    if(!pimpl) {
        system->Release();
        return false;
    }

    // Skip if already registered //
    if(pimpl->RegisteredScriptSystems.find(name) != pimpl->RegisteredScriptSystems.end()) {
        system->Release();
        return false;
    }

    // Create a wrapper for it //
    // The wrapper handles unreferencing the handle
    pimpl->RegisteredScriptSystems[name] = std::make_unique<ScriptSystemWrapper>(name, system);

    // Might as well call Init now as other systems are almost certainly initialized as well //
    pimpl->RegisteredScriptSystems[name]->Init(this);
    return true;
}

DLLEXPORT bool GameWorld::UnregisterScriptSystem(const std::string& name)
{
    // if called after release
    if(!pimpl) {
        return false;
    }

    const auto iter = pimpl->RegisteredScriptSystems.find(name);
    if(iter == pimpl->RegisteredScriptSystems.end()) {
        return false;
    }

    iter->second->Release();
    pimpl->RegisteredScriptSystems.erase(iter);

    return true;
}

DLLEXPORT asIScriptObject* GameWorld::GetScriptSystem(const std::string& name)
{
    // if called after release
    if(!pimpl)
        return nullptr;

    auto iter = pimpl->RegisteredScriptSystems.find(name);

    if(iter == pimpl->RegisteredScriptSystems.end()) {

        LOG_ERROR("GameWorld: GetScriptSystemDirect: world has no system called: " + name);
        return nullptr;
    }

    return iter->second->GetASImplementationObject();
}
// ------------------------------------ //
DLLEXPORT void GameWorld::OnUnLinkedFromWindow(Window* window, Ogre::Root* ogre)
{
    if(!LinkedToWindow) {
        LOG_WARNING(
            "GameWorld: unlink from window called while this wasn't linked to a window");
        return;
    }

    if(window != LinkedToWindow) {

        throw InvalidArgument("GameWorld attempted to be unlinked from window that wasn't the "
                              "one it was linked to");
    }

    ogre->getCompositorManager2()->removeWorkspace(WorldWorkspace);
    WorldWorkspace = nullptr;
    LinkedToWindow = nullptr;

    if(!TickWhileInBackground) {
        _DoSuspendSystems();
    }

    InBackground = true;
}

DLLEXPORT void GameWorld::OnLinkToWindow(Window* window, Ogre::Root* ogre)
{
    LEVIATHAN_ASSERT(WorldsScene, "World is not initialized");

    if(!window)
        throw InvalidArgument("GameWorld attempted to be linked to a nullptr window");

    if(LinkedToWindow || WorldWorkspace) {

        throw InvalidArgument(
            "GameWorld attempted to be linked to a window while it is already linked");
    }

    LinkedToWindow = window;

    // Create the workspace for this scene //
    // Which will be rendered before the overlay workspace but after potential
    // clearing workspace
    WorldWorkspace = ogre->getCompositorManager2()->addWorkspace(WorldsScene,
        LinkedToWindow->GetOgreWindow(), WorldSceneCamera, "WorldsWorkspace", true, 0);

    if(!TickWhileInBackground) {
        _DoResumeSystems();
    }

    InBackground = false;
}

DLLEXPORT void GameWorld::SetRunInBackground(bool tickinbackground)
{
    TickWhileInBackground = tickinbackground;
}

// // ------------------ RayCastHitEntity ------------------ //
// DLLEXPORT Leviathan::RayCastHitEntity::RayCastHitEntity(
//     const NewtonBody* ptr /*= nullptr*/, const float& tvar, RayCastData* ownerptr) :
//     HitEntity(ptr),
//     HitVariable(tvar)
// {
//     if(ownerptr) {
//         HitLocation = ownerptr->BaseHitLocationCalcVar * HitVariable;
//     } else {
//         HitLocation = (Float3)0;
//     }
// }

// DLLEXPORT bool Leviathan::RayCastHitEntity::HasHit()
// {
//     return HitEntity != nullptr;
// }

// DLLEXPORT bool Leviathan::RayCastHitEntity::DoesBodyMatchThisHit(NewtonBody* other)
// {
//     return HitEntity == other;
// }

// DLLEXPORT Float3 Leviathan::RayCastHitEntity::GetPosition()
// {
//     return HitLocation;
// }

// DLLEXPORT RayCastHitEntity& Leviathan::RayCastHitEntity::operator=(
//     const RayCastHitEntity& other)
// {
//     HitEntity = other.HitEntity;
//     HitVariable = other.HitVariable;
//     HitLocation = other.HitLocation;

//     return *this;
// }
// // ------------------ RayCastData ------------------ //
// DLLEXPORT Leviathan::RayCastData::RayCastData(
//     int maxcount, const Float3& from, const Float3& to) :
//     MaxCount(maxcount),
//     // Formula based on helpful guy on Newton forums
//     BaseHitLocationCalcVar(from + (to - from))
// {
//     // Reserve memory for maximum number of objects //
//     HitEntities.reserve(maxcount);
// }

// DLLEXPORT Leviathan::RayCastData::~RayCastData()
// {
//     // We want to release all hit data //
//     SAFE_RELEASE_VECTOR(HitEntities);
// }
