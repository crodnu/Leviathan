// Leviathan Game Engine
// Copyright (c) 2012-2016 Henri Hyyryläinen
#pragma once

//! \file This file contains common components for entities
#include "Define.h"
// ------------------------------------ //
#include "Common/Types.h"
#include "Common/SFMLPackets.h"
#include "Objects/Constraints.h"
#include "Newton/PhysicalWorld.h"

#include "Component.h"

#include "boost/circular_buffer.hpp"

#include <functional>


namespace Leviathan{

//! brief Class containing residue static helper functions
class ComponentHelpers{

    ComponentHelpers() = delete;

    //! \brief Creates a component state from a packet
    static std::shared_ptr<ComponentState> DeSerializeState(sf::Packet &packet);
};

//! \brief Entity has position and direction it is looking at
//! \note Any possible locking needs to be handled by the caller
class Position : public Component{
public:
        
    struct Data : public ComponentData{

        Data(const Float3 &position, const Float4 &orientation) :
            _Position(position), _Orientation(orientation)
        {}

        Float3 _Position;
        Float4 _Orientation;
    };

public:

    //! \brief Creates at specific position
    inline Position(const Data &data) : Component(TYPE),
                                        Members(data){}
        
    Data Members;
    
    static constexpr auto TYPE = COMPONENT_TYPE::Position;
};

//! \brief Entity has an Ogre scene node
//! \note By default this is not marked. If you change Hidden set as marked to
//! update Node state
class RenderNode : public Component{
public:

    DLLEXPORT RenderNode(Ogre::SceneManager* scene);

    //! \brief Gracefully releases while world is still valid
    DLLEXPORT void Release(Ogre::SceneManager* worldsscene);
    
    Ogre::SceneNode* Node = nullptr;

    //! Sets objects attached to the node to be hidden or visible
    bool Hidden = false;

    static constexpr auto TYPE = COMPONENT_TYPE::RenderNode;
};

//! \brief Entity is sendable to clients
//! \note This will only be in the entity on the server
class Sendable : public Component{
public:
    //! \note This is not thread safe do not call CheckReceivedPackets and AddSentPacket
    //! at the same time
    //! \todo Make sure that CheckReceivedPackages is called for entities that have
    //! stopped moving ages ago to free up memory
    class ActiveConnection{
    public:

        inline ActiveConnection(std::shared_ptr<Connection> connection) :
            CorrespondingConnection(connection), LastConfirmedTickNumber(-1) {}

        //! \brief Checks has any packet been successfully received and updates
        //! last confirmed
        DLLEXPORT void CheckReceivedPackets();

        //! \brief Adds a package to be checked for finalization in CheckReceivedPackages
        inline void AddSentPacket(int tick, std::shared_ptr<ComponentState> state,
            std::shared_ptr<SentNetworkThing> packet) 
        {
            SentPackets.push_back(std::make_tuple(tick, state, packet));
        }
            
        std::shared_ptr<Connection> CorrespondingConnection;

        //! Data used to build a delta update packet
        //! \note This is set to be the last known successfully sent state to
        //! avoid having to resend intermediate steps
        std::shared_ptr<ComponentState> LastConfirmedData;

        //! The tick number of the confirmed state
        //! If a state is confirmed as received that has number higher than this
        // LastConfirmedData will be replaced.
        int LastConfirmedTickNumber;

        //! Holds packets sent to this connection that haven't failed or been received yet
        //! \todo Move this into GameWorld to keep a single list of connected players
        std::vector<std::tuple<int, std::shared_ptr<ComponentState>,
                        std::shared_ptr<SentNetworkThing>>> SentPackets;
    };
        
public:
     
    inline Sendable() : Component(TYPE){ }

    inline void AddConnectionToReceivers(std::shared_ptr<Connection> connection) {

        UpdateReceivers.push_back(std::make_shared<ActiveConnection>(connection));
    }

    //! Clients we have already sent a state to
    std::vector<std::shared_ptr<ActiveConnection>> UpdateReceivers;

    static constexpr auto TYPE = COMPONENT_TYPE::Sendable;
};

//! \brief Entity is received from a server
//!
//! Sendable counterpart on the client
class Received : public Component{
public:
    //! \brief Storage class for ObjectDeltaStateData
    //! \todo Possibly add move constructors
    class StoredState{
    public:
        inline StoredState(std::shared_ptr<ComponentState> safedata, int tick,
            void* data) :
            DeltaData(safedata), Tick(tick), DirectData(data){}
        
        std::shared_ptr<ComponentState> DeltaData;

        //! Tick number, should be retrieved from DeltaData
        int Tick;

        //! This avoids using dynamic_cast
        void* DirectData;
    };
public:

    inline Received() : Component(TYPE),
                        ClientStateBuffer(BASESENDABLE_STORED_RECEIVED_STATES){}

    DLLEXPORT void GetServerSentStates(StoredState const** first,
        StoredState const** second, int tick, float &progress) const;

    //! Client side buffer of past states
    boost::circular_buffer<StoredState> ClientStateBuffer;

    //! If true this uses local control and will send updates to the server
    bool LocallyControlled = false;

    static constexpr auto TYPE = COMPONENT_TYPE::Received;
};


//! \brief Entity has a box for geometry/model, possibly also physics
class BoxGeometry : public Component{
public:
    inline BoxGeometry(const Float3 &size, const std::string &material) :
        Component(TYPE),
        Sizes(size), Material(material){}
    
    //! Size along the axises
    Float3 Sizes;

    //! Rendering surface material name
    std::string Material;

    //! Entity created from a box mesh
    Ogre::Item* GraphicalObject = nullptr;

    static constexpr auto TYPE = COMPONENT_TYPE::BoxGeometry;
};

//! \brief Entity has a model
class Model : public Component{
public:
    DLLEXPORT Model(Ogre::SceneManager* scene, Ogre::SceneNode* parent,
        const std::string &meshname);

    //! \brief Destroys GraphicalObject
    DLLEXPORT void Release(Ogre::SceneManager* scene);
        
    //! The entity that has this model's mesh loaded
    Ogre::Item* GraphicalObject = nullptr;

    static constexpr auto TYPE = COMPONENT_TYPE::Model;
};

//! \brief Plane component
//!
//! Creates a static mesh for this
class Plane : public Component{
public:
    DLLEXPORT Plane(Ogre::SceneManager* scene, Ogre::SceneNode* parent,
        const std::string &material, const Ogre::Plane &plane, const Float2 &size);

    //! \brief Destroys GraphicalObject
    DLLEXPORT void Release(Ogre::SceneManager* scene);

    //! The plane that this component creates
    Ogre::Item* GraphicalObject = nullptr;

    const std::string GeneratedMeshName;

    static constexpr auto TYPE = COMPONENT_TYPE::Plane;
};


//! \brief Entity has a physical component
//! \pre Entity has Position component
//! \todo Global Newton lock
class Physics : public Component{
public:

    //! \brief Holder for information regarding a single force
    class ApplyForceInfo{
    public:
        //! \note Pass NULL for name if not used, avoid passing empty strings
        //! \param name The name to assign. This will be deleted by a std::unique_ptr
        ApplyForceInfo(bool addmass,
            std::function<Float3(ApplyForceInfo* instance, Physics &object)> getforce,
            std::unique_ptr<std::string> name = nullptr) :
            OptionalName(move(name)), MultiplyByMass(addmass), Callback(getforce){}
        
        ApplyForceInfo(const ApplyForceInfo &other) :
            MultiplyByMass(other.MultiplyByMass), Callback(other.Callback)
        {
            if(other.OptionalName)
                OptionalName = std::make_unique<std::string>(*other.OptionalName);
        }
        
        ApplyForceInfo(ApplyForceInfo &&other) :
            OptionalName(move(other.OptionalName)),
            MultiplyByMass(std::move(other.MultiplyByMass)), Callback(move(other.Callback)){}

        ApplyForceInfo& operator =(const ApplyForceInfo &other){

            if(other.OptionalName)
                OptionalName = std::make_unique<std::string>(*other.OptionalName);

            MultiplyByMass = other.MultiplyByMass;
            Callback = other.Callback;

            return *this;
        }

        //! Set a name when you don't want other non-named forces to override this
        std::unique_ptr<std::string> OptionalName;
        
        //! Whether to multiply the force by mass, makes acceleration constant with
        //! different masses
        bool MultiplyByMass;
        
        //! The callback which returns the force
        //! \todo Allow deleting this force from the callback
        std::function<Float3(ApplyForceInfo* instance, Physics &object)> Callback;
    };

    struct BasePhysicsData{

        Float3 Velocity;
        Float3 Torque;
    };

    struct Data{

        ObjectID id;
        GameWorld* world;
        Position &updatepos;
        Sendable* updatesendable;
    };
    
public:
    
    inline Physics(const Data &args) : Component(TYPE),
        World(args.world),
        _Position(args.updatepos), ThisEntity(args.id),
        UpdateSendable(args.updatesendable){}

    DLLEXPORT void Release();
        
    DLLEXPORT void GiveImpulse(const Float3 &deltaspeed, const Float3 &point = Float3(0));

    //! \brief Adds an apply force
    //! \note Overwrites old forces with the same name
    //! \param pointertohandle Pointer to the force which will be deleted by this
    DLLEXPORT void ApplyForce(ApplyForceInfo* pointertohandle);

    //! \brief Removes an existing ApplyForce
    //! \param name name of force to delete, pass empty std::string to delete the
    //! default named force
    DLLEXPORT bool RemoveApplyForce(const std::string &name);

    //! \brief Sets absolute velocity of the object
    DLLEXPORT void SetVelocity(const Float3 &velocities);

    //! \brief Gets the absolute velocity
    DLLEXPORT Float3 GetVelocity() const;

    inline NewtonBody* GetBody() const{

        return Body;
    }

    //! \brief Sets the torque of the body
    //! \see GetBodyTorque
    DLLEXPORT void SetTorque(const Float3 &torque);

    //! \brief Gets the torque of the body (rotational velocity)
    DLLEXPORT Float3 GetTorque();

    //! \brief Sets the physical material ID of this object
    //! \note You have to fetch the ID from the world's corresponding PhysicalMaterialManager
    DLLEXPORT void SetPhysicalMaterialID(int ID);

    //! \brief Sets the linear dampening which slows down the object
    //! \param factor The factor to set. Must be between 0.f and 1.f. Default is 0.1f
    //! \note This can be used to set the viscosity of the substance the object is in
    //! for example to mimic drag in water (this needs verification...)
    //!
    //! More on this in the Newton wiki here:
    //! http://newtondynamics.com/wiki/index.php5?title=NewtonBodySetLinearDamping
    DLLEXPORT void SetLinearDampening(float factor = 0.1f);

    //! \brief Applies physical state from holder object
    DLLEXPORT void ApplyPhysicalState(const BasePhysicsData &data);

        

    // default physics callbacks that are fine in most cases //
    // Don't forget to pass the user data as BaseObject if using these //
    static void ApplyForceAndTorgueEvent(const NewtonBody* const body, dFloat timestep,
        int threadIndex);
        
    static void DestroyBodyCallback(const NewtonBody* body);

    static void PhysicsMovedEvent(const NewtonBody* const body, const dFloat* const matrix,
        int threadIndex);

        
    //! \brief Adds all applied forces together
    Float3 _GatherApplyForces(const float &mass);
        
    //! \brief Destroys the physical body
    DLLEXPORT void Release(NewtonWorld* world);

    //! \brief Moves the physical body to the specified position
    DLLEXPORT void JumpTo(Position &target);

    DLLEXPORT bool SetPosition(const Float3 &pos, const Float4 &orientation);

        
        
    NewtonCollision* Collision = nullptr;
    NewtonBody* Body = nullptr;

    //! The set physical material
    //! If none is set this defaults to -1
    //! The default material ID from GetDefaultPhysicalMaterialID might be applied
    int AppliedPhysicalMaterial = -1;

    bool ApplyGravity = true;

    //! Non-newton access to mass
    float Mass = 0.f;

    std::list<std::shared_ptr<ApplyForceInfo>> ApplyForceList;

    //! Used to access gravity data
    GameWorld* World = nullptr;

    //! Physics object requires a position
    Position& _Position;

    //! For access from physics callbacks
    ObjectID ThisEntity;

    // Optional access to other components that can be used for marking when physics object
    // moves
    Sendable* UpdateSendable = nullptr;

    static constexpr auto TYPE = COMPONENT_TYPE::Physics;
};

class ManualObject : public Component{
public:

    inline ManualObject() : Component(TYPE){}

    DLLEXPORT void Release(Ogre::SceneManager* scene);

    Ogre::ManualObject* Object = nullptr;

    //! When not empty the ManualObject has been created into an actual mesh
    //! that needs to be destroyed on release
    std::string CreatedMesh;

    static constexpr auto TYPE = COMPONENT_TYPE::ManualObject;
};


// class Parent : public Component{
// public:

//     struct Data{

//         std::vector<ObjectID> EntityIDs;
//     };
        
// public:
//     DLLEXPORT Parent();

//     DLLEXPORT Parent(const Data &data, GameWorld* world, Lock &worldlock);

//     //! \brief Removes child object without notifying it
//     DLLEXPORT void RemoveChildNoNotify(Parentable* which);

//     //! \brief Removes all children notifying them
//     DLLEXPORT void RemoveChildren();

//     DLLEXPORT void AddDataToPacket(sf::Packet &packet) const;

//     //! \note The packet needs to be checked that it is still valid after this call
//     DLLEXPORT static Data LoadDataFromPacket(sf::Packet &packet);

//     //! \brief Does everything necessary to attach a child
//     DLLEXPORT void AddChild(ObjectID childid, Parentable &child);

//     //! Attached children which can be moved at certain times
//     //! \todo Make improvements to component lookup performance through this
//     std::vector<std::tuple<ObjectID, Parentable*>> Children;
// };



// class Constraintable : public Component{
// public:
//     //! \param world Used to allow created constraints to access world data (including physics)
//     DLLEXPORT Constraintable(ObjectID id, GameWorld* world);

//     //! \brief Destroys all constraints attached to this
//     DLLEXPORT ~Constraintable();

//     //! Creates a constraint between this and another object
//     //! \warning DO NOT store the returned value (since that reference isn't counted)
//     //! \note Before the constraint is actually finished, you need to
//     //! call ->SetParameters() on the returned ptr
//     //! and then ->Init() and then let go of the ptr
//     //! \note If you do not want to allow constraints where child is NULL you have to
//     //! check if child is NULL before calling this function
//     template<class ConstraintClass, typename... Args>
//     std::shared_ptr<ConstraintClass> CreateConstraintWith(Constraintable &other,
//         Args&&... args)
//     {
//         auto tmpconstraint = std::make_shared<ConstraintClass>(World, *this, other, args...);

//         if(tmpconstraint)
//             _NotifyCreate(tmpconstraint, other);
            
//         return tmpconstraint;
//     }

//     DLLEXPORT void RemoveConstraint(BaseConstraint* removed);

//     DLLEXPORT void AddConstraint(std::shared_ptr<BaseConstraint> added);

// protected:

//     //! \brief Notifies the other object and the GameWorld of the new constraint
//     DLLEXPORT void _NotifyCreate(std::shared_ptr<BaseConstraint> newconstraint,
//         Constraintable &other);

// public:
        
        
//     std::vector<std::shared_ptr<BaseConstraint>> PartInConstraints;

//     GameWorld* World;

//     //! ID for other component lookup
//     ObjectID PartOfEntity;
// };

// class Trail : public Component{
// public:

//     struct ElementProperties{
//         ElementProperties(const Float4 &initialcolour,
//             const Float4 &colourchange, const float &initialsize, const float &sizechange) : 
//             InitialColour(initialcolour), ColourChange(colourchange), InitialSize(initialsize),
//             SizeChange(sizechange)
//         {

//         }
        
//         ElementProperties(const Float4 &initialcolour,
//             const float &initialsize) : 
//             InitialColour(initialcolour), ColourChange(0), InitialSize(initialsize),
//             SizeChange(0)
//         {

//         }

//         ElementProperties() :
//             InitialColour(1), ColourChange(0), InitialSize(1), SizeChange(0)
//         {

//         }

//         Float4 InitialColour;
//         Float4 ColourChange;
//         float InitialSize;
//         float SizeChange;
//     };

//     struct Properties{
//     public:
//         Properties(size_t maxelements, float lenght, float maxdistance,
//             bool castshadows = false) :
//             TrailLenght(lenght), MaxDistance(maxdistance),
//             MaxChainElements(maxelements), CastShadows(castshadows), Elements(1)
//         {
//         }
        
//         float TrailLenght;
//         float MaxDistance;
//         size_t MaxChainElements;
//         bool CastShadows;

//         std::vector<ElementProperties> Elements;
//     };
        

// public:

//     DLLEXPORT Trail(RenderNode* node, const std::string &materialname,
//         const Properties &variables);

//     //! \brief Sets properties on the trail object
//     //! \pre Ogre objects have been created
//     //! \param force If set to true all settings will be applied
// 	DLLEXPORT bool SetTrailProperties(const Properties &variables, bool force = false);

//     //! \brief Destroys the TrailEntity
//     DLLEXPORT void Release(Ogre::SceneManager* scene);

//     //! The trail entity which is attached at the root scene node and follows our RenderNode
//     //! component around
// 	Ogre::RibbonTrail* TrailEntity = nullptr;

//     //! For ease of use direct access to ogre node is required
//     //! Not valid in non-gui mode
//     RenderNode* _RenderNode = nullptr;

//     //! The used trail material
//     std::string Material;

//     //! Current settings, when updating settings only changed ones are applied
//     Properties CurrentSettings;
// };

// //! \todo Add the Markers to the actual world for sending over the network individually
// class PositionMarkerOwner : public Component{
// public:

//     struct Data{

//         std::vector<std::tuple<ObjectID, Float3, Float4>> EntityPositions;
//     };
        
// public:
//     DLLEXPORT PositionMarkerOwner();

//     //! \brief Create with automatically created positions
//     DLLEXPORT PositionMarkerOwner(const Data &positions, GameWorld* world,
//         Lock &worldlock);

//     //! \brief Queues destruction and clears the list of Markers
//     DLLEXPORT void Release(GameWorld* world, Lock &worldlock);

//     //! Adds a node
//     //! \todo Allow not deleting entities on release
//     DLLEXPORT void Add(ObjectID entity, Position& pos);

//     DLLEXPORT void AddDataToPacket(sf::Packet &packet) const;

//     //! \note The packet needs to be checked that it is valid after this call
//     DLLEXPORT static Data LoadDataFromPacket(sf::Packet &packet);
        
//     std::vector<std::tuple<ObjectID, Position*>> Markers;
// };


//! \brief Properties that a camera entity has (will also need a Position component)
class Camera : public Component{
public:

    //! \brief Creates at specific position
    inline Camera(uint8_t fovy = 60, bool soundperceiver = true) :
        Component(TYPE), FOVY(fovy), SoundPerceiver(soundperceiver)
    {

    }

    //! Y-axis based field of view.
    //! \warning This is different than the usual x-axis based field of view!
    //! See the Ogre manual for details: Ogre::Frustum::setFOVy (const Radian & fovy )
    //!
    //! Normal range is 45 to 60
    uint8_t FOVY;
    bool SoundPerceiver;
    // TODO: orthographic
    // bool Orthographic;
    
    static constexpr auto TYPE = COMPONENT_TYPE::Camera;
};

}

#ifdef LEAK_INTO_GLOBAL
using ApplyForceInfo = Leviathan::Physics::ApplyForceInfo;
#endif


