#ifndef OPENRAVE_FCL_COLLISION
#define OPENRAVE_FCL_COLLISION

#include <boost/unordered_set.hpp>
#include <boost/lexical_cast.hpp>
#include <openrave/utils.h>

#include "fclspace.h"

typedef FCLSpace::KinBodyInfoConstPtr KinBodyInfoConstPtr;
typedef FCLSpace::KinBodyInfoPtr KinBodyInfoPtr;


class FCLCollisionChecker : public OpenRAVE::CollisionCheckerBase
{
public:


    struct CollisionCallbackData {
        CollisionCallbackData(boost::shared_ptr<FCLCollisionChecker> pchecker, CollisionReportPtr report) : _pchecker(pchecker), _report(report), bselfCollision(false), bstop(false), _bCollision(false)
        {
            if( _pchecker->GetEnv()->HasRegisteredCollisionCallbacks() && !_report ) {
                _report.reset(new CollisionReport());
            }

            // TODO : What happens if we have CO_AllGeometryContacts set and not CO_Contacts ?
            if( !!report && !!(_pchecker->GetCollisionOptions() & OpenRAVE::CO_Contacts) ) {
                _request.num_max_contacts = _pchecker->GetNumMaxContacts();
                _request.enable_contact = true;
            }

            if( !!_report ) {
                _report->Reset(_pchecker->GetCollisionOptions());
            }
        }


        boost::shared_ptr<FCLCollisionChecker> _pchecker;
        fcl::CollisionRequest _request;
        fcl::CollisionResult _result;
        CollisionReportPtr _report;
        std::list<EnvironmentBase::CollisionCallbackFn> listcallbacks;

        bool bselfCollision;
        bool bstop;
        bool _bCollision;
    };

    typedef boost::shared_ptr<CollisionCallbackData> CollisionCallbackDataPtr;


    /// \brief Wraps a temporary broadphase manager to prepare for a collision against environment. Unregister objects from the environment as needed and restore them upon destruction
    class TemporaryManagerAgainstEnv {
public:
    TemporaryManagerAgainstEnv(boost::shared_ptr<FCLSpace> pfclspace, BroadPhaseCollisionManagerPtr manager = BroadPhaseCollisionManagerPtr()) : _pfclspace(pfclspace)
        {
          _bownsManager = !manager;
          if( _bownsManager ) {
            _manager = _pfclspace->CreateManager();
          } else {
            _manager = manager;
            CollisionGroup tmpGroup;
            manager->getObjects(tmpGroup);
            UnregisterObjects(_pfclspace->GetEnvManager(), tmpGroup);
          }
        }

        ~TemporaryManagerAgainstEnv() {
            CollisionGroup tmpGroup;
            _manager->getObjects(tmpGroup);
            if( _bownsManager) {
              _manager->clear();
            }
            _pfclspace->GetEnvManager()->registerObjects(tmpGroup);
            _pfclspace->GetEnvManager()->registerObjects(vexcluded);
        }

        void Register(KinBodyConstPtr pbody) {
          BOOST_ASSERT( _bownsManager );
            KinBodyInfoPtr pinfo = _pfclspace->GetInfo(pbody);
            BOOST_ASSERT( pinfo->GetBody() == pbody );

            CollisionGroup tmpGroup;
            pinfo->_bodyManager->getObjects(tmpGroup);
            if( pbody->IsEnabled() ) {
                _manager->registerObjects(tmpGroup);
            } else {
                copy(tmpGroup.begin(), tmpGroup.end(), back_inserter(vexcluded));
            }
            UnregisterObjects(_pfclspace->GetEnvManager(), tmpGroup);
        }

        void Register(LinkConstPtr plink) {
          BOOST_ASSERT( _bownsManager );
            CollisionObjectPtr pcoll = _pfclspace->GetLinkBV(plink);
            _manager->registerObject(pcoll.get());
            _pfclspace->GetEnvManager()->unregisterObject(pcoll.get());
        }

        void Exclude(KinBodyConstPtr pbody) {
            KinBodyInfoPtr pinfo = _pfclspace->GetInfo(pbody);
            BOOST_ASSERT( pinfo->GetBody() == pbody );

            CollisionGroup tmpGroup;
            pinfo->_bodyManager->getObjects(tmpGroup);
            copy(tmpGroup.begin(), tmpGroup.end(), back_inserter(vexcluded));
            UnregisterObjects(_pfclspace->GetEnvManager(), tmpGroup);
        }

        void Exclude(LinkConstPtr plink) {
            CollisionObjectPtr pcoll = _pfclspace->GetLinkBV(plink);
            vexcluded.push_back(pcoll.get());
            _pfclspace->GetEnvManager()->unregisterObject(pcoll.get());
        }


        void Collide(CollisionCallbackDataPtr pquery) {
          BroadPhaseCollisionManagerPtr envManager = _pfclspace->GetEnvManager();
          _manager->setup();
          envManager->setup();
          envManager->collide(_manager.get(), pquery.get(), &FCLCollisionChecker::NarrowPhaseCheckCollision);
        }

        BroadPhaseCollisionManagerPtr GetManager() {
            return _manager;
        }

private:
        boost::shared_ptr<FCLSpace> _pfclspace;
        bool _bownsManager;
        BroadPhaseCollisionManagerPtr _manager;
        CollisionGroup vexcluded;
    };


    /// \brief Wraps 2 temporary broadphase manager to prepare for a collision
    class TemporaryManager {
public:
    TemporaryManager(boost::shared_ptr<FCLSpace> pfclspace, BroadPhaseCollisionManagerPtr manager1 = BroadPhaseCollisionManagerPtr(), BroadPhaseCollisionManagerPtr manager2 = BroadPhaseCollisionManagerPtr()) : _pfclspace(pfclspace)
        {
          _bownsManager1 = !manager1;
          _bownsManager2 = !manager2;

          _manager1 = _bownsManager1 ? _pfclspace->CreateManager() : manager1 ;
          _manager2 = _bownsManager2 ? _pfclspace->CreateManager() : manager2 ;
        }

        void Register(KinBodyConstPtr pbody, bool firstManager) {
          BOOST_ASSERT( (firstManager && _bownsManager1) || _bownsManager2 );
            KinBodyInfoPtr pinfo = _pfclspace->GetInfo(pbody);
            BOOST_ASSERT( pinfo->GetBody() == pbody );

            CollisionGroup tmpGroup;
            pinfo->_bodyManager->getObjects(tmpGroup);
            if( pbody->IsEnabled() ) {
                (firstManager ? _manager1 : _manager2)->registerObjects(tmpGroup);
            }
        }

        void Register(LinkConstPtr plink, bool firstManager) {
          BOOST_ASSERT( (firstManager && _bownsManager1) || _bownsManager2 );
            CollisionObjectPtr pcoll = _pfclspace->GetLinkBV(plink);
            (firstManager ? _manager1 : _manager2)->registerObject(pcoll.get());
        }


        void Collide(CollisionCallbackDataPtr pquery) {
          _manager1->setup();
          _manager2->setup();
          _manager1->collide(_manager2.get(), pquery.get(), &FCLCollisionChecker::NarrowPhaseCheckCollision);
        }

        BroadPhaseCollisionManagerPtr GetManager(bool firstManager) {
            return (firstManager ? _manager1 : _manager2);
        }

private:
        boost::shared_ptr<FCLSpace> _pfclspace;
        bool _bownsManager1, _bownsManager2;
        BroadPhaseCollisionManagerPtr _manager1, _manager2;
    };

    typedef boost::shared_ptr<TemporaryManager> TemporaryManagerPtr;
    typedef boost::shared_ptr<TemporaryManagerAgainstEnv> TemporaryManagerAgainstEnvPtr;



    FCLCollisionChecker(OpenRAVE::EnvironmentBasePtr penv, std::istream& sinput)
        : OpenRAVE::CollisionCheckerBase(penv), bisSelfCollisionChecker(true)
    {
        _userdatakey = std::string("fclcollision") + boost::lexical_cast<std::string>(this);
        _fclspace.reset(new FCLSpace(penv, _userdatakey));
        _options = 0;
        _numMaxContacts = std::numeric_limits<int>::max(); // TODO
        __description = ":Interface Author:\n\nFlexible Collision Library collision checker";

        RegisterCommand("SetBroadphaseAlgorithm", boost::bind(&FCLCollisionChecker::_SetBroadphaseAlgorithm, this, _1, _2), "sets the broadphase algorithm (Naive, SaP, SSaP, IntervalTree, DynamicAABBTree, DynamicAABBTree_Array)");

        RegisterCommand("SetBVHRepresentation", boost::bind(&FCLCollisionChecker::_SetBVHRepresentation, this, _1, _2), "sets the Bouding Volume Hierarchy representation for meshes (AABB, OBB, OBBRSS, RSS, kIDS)");
        // TODO : check that the coordinate are in the right order
        RegisterCommand("SetSpatialHashingBroadPhaseAlgorithm", boost::bind(&FCLCollisionChecker::SetSpatialHashingBroadPhaseAlgorithm, this, _1, _2), "sets the broadphase algorithm to spatial hashing with (cell size) (scene min x) (scene min y) (scene min z) (scene max x) (scene max y) (scene max z)");
        RAVELOG_VERBOSE_FORMAT("FCLCollisionChecker %s created in env %d", _userdatakey%penv->GetId());

        std::string broadphasealg, bvhrepresentation;
        sinput >> broadphasealg >> bvhrepresentation;
        if( broadphasealg != "" ) {
            if( broadphasealg == "SpatialHashing" ) {
                std::ostream nullout(nullptr);
                SetSpatialHashingBroadPhaseAlgorithm(nullout, sinput);
            } else {
                _fclspace->SetBroadphaseAlgorithm(broadphasealg);
            }
        }
        if( bvhrepresentation != "" ) {
            _fclspace->SetBVHRepresentation(bvhrepresentation);
        }
    }

    virtual ~FCLCollisionChecker() {
        RAVELOG_VERBOSE_FORMAT("FCLCollisionChecker %s destroyed in env %d", _userdatakey%GetEnv()->GetId());
        DestroyEnvironment();
    }

    void Clone(InterfaceBaseConstPtr preference, int cloningoptions)
    {
        CollisionCheckerBase::Clone(preference, cloningoptions);
        boost::shared_ptr<FCLCollisionChecker const> r = boost::dynamic_pointer_cast<FCLCollisionChecker const>(preference);
        _fclspace->SetGeometryGroup(r->GetGeometryGroup());
        _fclspace->SetBVHRepresentation(r->GetBVHRepresentation());

        std::string const &broadphaseAlgorithm = r->GetBroadphaseAlgorithm();
        if( broadphaseAlgorithm == "SpatialHashing" ) {
            _fclspace->SetSpatialHashingBroadPhaseAlgorithm(*r->GetSpatialHashData());
        } else {
            _fclspace->SetBroadphaseAlgorithm(broadphaseAlgorithm);
        }
        _options = r->_options;
        _numMaxContacts = r->_numMaxContacts;
        RAVELOG_VERBOSE(str(boost::format("FCL User data cloning env %d into env %d") % r->GetEnv()->GetId() % GetEnv()->GetId()));
    }

    boost::shared_ptr<const SpatialHashData> GetSpatialHashData() const {
        return _fclspace->GetSpatialHashData();
    }

    int GetNumMaxContacts() const {
        return _numMaxContacts;
    }

    void SetNumMaxContacts(int numMaxContacts) {
        _numMaxContacts = numMaxContacts;
    }

    void SetGeometryGroup(const std::string& groupname)
    {
        _fclspace->SetGeometryGroup(groupname);
    }

    const std::string& GetGeometryGroup() const
    {
        return _fclspace->GetGeometryGroup();
    }


    virtual bool SetCollisionOptions(int collision_options)
    {
        _options = collision_options;

        // TODO : remove when distance is implemented
        if( _options & OpenRAVE::CO_Distance ) {
            return false;
        }

        if( _options & OpenRAVE::CO_RayAnyHit ) {
            return false;
        }

        return true;
    }

    virtual int GetCollisionOptions() const
    {
        return _options;
    }

    virtual void SetTolerance(OpenRAVE::dReal tolerance)
    {
    }

    std::string const& GetBroadphaseAlgorithm() const {
        return _fclspace->GetBroadphaseAlgorithm();
    }

    /// Sets the broadphase algorithm for collision checking
    /// The input algorithm can be one of : Naive, SaP, SSaP, IntervalTree, DynamicAABBTree, DynamicAABBTree_Array, SpatialHashing
    /// e.g. "SetBroadPhaseAlgorithm DynamicAABBTree"
    /// In order to use SpatialHashing, the spatial hashing data must be set beforehand with SetSpatialHashingBroadPhaseAlgorithm
    bool _SetBroadphaseAlgorithm(ostream& sout, istream& sinput)
    {
        std::string algorithm;
        sinput >> algorithm;
        _fclspace->SetBroadphaseAlgorithm(algorithm);
        return !!sinput;
    }

    std::string const& GetBVHRepresentation() const {
        return _fclspace->GetBVHRepresentation();
    }

    /// Sets the bounding volume hierarchy representation which can be one of
    /// AABB, OBB, RSS, OBBRSS, kDOP16, kDOP18, kDOP24, kIOS
    /// e.g. "SetBVHRepresentation OBB"
    bool _SetBVHRepresentation(ostream& sout, istream& sinput)
    {
        std::string type;
        sinput >> type;
        _fclspace->SetBVHRepresentation(type);
        return !!sinput;
    }

    /// Sets the spatial hashing data and switch to the spatial hashing broadphase algorithm
    /// e.g. SetSpatialHashingBroadPhaseAlgorithm cell_size scene_min_x scene_min_y scene_min_z scene_max_x scene_max_y scene_max_z
    bool SetSpatialHashingBroadPhaseAlgorithm(ostream& sout, istream& sinput)
    {
        fcl::FCL_REAL cell_size;
        fcl::Vec3f scene_min, scene_max;
        sinput >> cell_size >> scene_min[0] >> scene_min[1] >> scene_min[2] >> scene_max[0] >> scene_max[1] >> scene_max[2];
        _fclspace->SetSpatialHashingBroadPhaseAlgorithm(SpatialHashData(cell_size, scene_min, scene_max));
        return !!sinput;
    }


    virtual bool InitEnvironment()
    {
        RAVELOG_VERBOSE(str(boost::format("FCL User data initializing %s in env %d") % _userdatakey % GetEnv()->GetId()));
        bisSelfCollisionChecker = false;
        vector<KinBodyPtr> vbodies;
        GetEnv()->GetBodies(vbodies);
        FOREACHC(itbody, vbodies) {
            InitKinBody(*itbody);
        }
        return true;
    }

    virtual void DestroyEnvironment()
    {
        RAVELOG_VERBOSE(str(boost::format("FCL User data destroying %s in env %d") % _userdatakey % GetEnv()->GetId()));
        _fclspace->DestroyEnvironment();
    }

    virtual bool InitKinBody(OpenRAVE::KinBodyPtr pbody)
    {
        FCLSpace::KinBodyInfoPtr pinfo = boost::dynamic_pointer_cast<FCLSpace::KinBodyInfo>(pbody->GetUserData(_userdatakey));
        if( !pinfo || pinfo->GetBody() != pbody ) {
            pinfo = _fclspace->InitKinBody(pbody);
        }
        return !pinfo;
    }

    virtual void RemoveKinBody(OpenRAVE::KinBodyPtr pbody)
    {
        _fclspace->RemoveUserData(pbody);
    }

    virtual bool CheckCollision(KinBodyConstPtr pbody1, CollisionReportPtr report = CollisionReportPtr())
    {
        static std::vector<KinBodyConstPtr> const vbodyexcluded;
        static std::vector<LinkConstPtr> const vlinkexcluded;

        // TODO : tailor this case when stuff become stable enough
        return CheckCollision(pbody1, vbodyexcluded, vlinkexcluded, report);
    }

    virtual bool CheckCollision(KinBodyConstPtr pbody1, KinBodyConstPtr pbody2, CollisionReportPtr report = CollisionReportPtr())
    {
        if( pbody1->GetLinks().size() == 0 || !pbody1->IsEnabled() ) {
            return false;
        }

        if( pbody2->GetLinks().size() == 0 || !pbody2->IsEnabled() ) {
            return false;
        }

        if( pbody1->IsAttached(pbody2) ) {
            return false;
        }

        // Do we really want to synchronize everything ?
        _fclspace->Synchronize();


        TemporaryManagerPtr ptmpManagerBody1Body2 = boost::make_shared<TemporaryManager>(_fclspace);
        // This is asymetric : we consider everything attached to an active link of the first body and anything attached to the second
        FillTemporaryManagerWithBody(pbody1, ptmpManagerBody1Body2, _options & OpenRAVE::CO_ActiveDOFs, true);
        FillTemporaryManagerWithBody(pbody2, ptmpManagerBody1Body2, false, false);

        if( _options & OpenRAVE::CO_Distance ) {
            RAVELOG_WARN("fcl doesn't support CO_Distance yet\n");
            return false; //TODO
        } else {
            CollisionCallbackDataPtr pquery = boost::make_shared<CollisionCallbackData>(shared_checker(), report);
            ptmpManagerBody1Body2->Collide(pquery);
            return pquery->_bCollision;
        }

    }

    virtual bool CheckCollision(LinkConstPtr plink,CollisionReportPtr report = CollisionReportPtr())
    {
        static std::vector<KinBodyConstPtr> const vbodyexcluded;
        static std::vector<LinkConstPtr> const vlinkexcluded;

        // TODO : tailor this case when stuff become stable enough
        return CheckCollision(plink, vbodyexcluded, vlinkexcluded, report);
    }

    virtual bool CheckCollision(LinkConstPtr plink1, LinkConstPtr plink2, CollisionReportPtr report = CollisionReportPtr())
    {
        if( !plink1->IsEnabled() || !plink2->IsEnabled() ) {
            return false;
        }

        // why do we synchronize everything ?
        _fclspace->Synchronize(plink1->GetParent());
        _fclspace->Synchronize(plink2->GetParent());

        CollisionObjectPtr pcollLink1 = _fclspace->GetLinkBV(plink1), pcollLink2 = _fclspace->GetLinkBV(plink2);

        if( _options & OpenRAVE::CO_Distance ) {
            RAVELOG_WARN("fcl doesn't support CO_Distance yet\n");
            return false; // TODO
        } else {
            CollisionCallbackDataPtr pquery = boost::make_shared<CollisionCallbackData>(shared_checker(), report);
            NarrowPhaseCheckCollision(pcollLink1.get(), pcollLink2.get(), pquery.get());
            return pquery->_bCollision;
        }
    }

    virtual bool CheckCollision(LinkConstPtr plink, KinBodyConstPtr pbody,CollisionReportPtr report = CollisionReportPtr())
    {
        OPENRAVE_ASSERT_OP(pbody->GetEnvironmentId(),!=,0);
        if( !plink->IsEnabled() ) {
            return false;
        }

        if( pbody->GetLinks().size() == 0 || !pbody->IsEnabled() ) {
            return false;
        }

        if( pbody->IsAttached(plink->GetParent()) ) {
            return false;
        }


        std::set<KinBodyPtr> attachedBodies;
        pbody->GetAttached(attachedBodies);

        _fclspace->Synchronize(plink->GetParent());
        FOREACH(itbody, attachedBodies) {
            _fclspace->Synchronize(*itbody);
        }

        TemporaryManagerPtr ptmpManagerBodyLink = boost::make_shared<TemporaryManager>(_fclspace);
        // seems that activeDOFs are not considered in oderave : the check is done but it will always return true
        FOREACH(itbody, attachedBodies) {
            ptmpManagerBodyLink->Register(*itbody, true);
        }
        ptmpManagerBodyLink->Register(plink, false);

        if( _options & OpenRAVE::CO_Distance ) {
            RAVELOG_WARN("fcl doesn't support CO_Distance yet\n");
            return false; // TODO
        } else {
            CollisionCallbackDataPtr pquery = boost::make_shared<CollisionCallbackData>(shared_checker(), report);
            ptmpManagerBodyLink->Collide(pquery);
            return pquery->_bCollision;
        }
    }

    virtual bool CheckCollision(LinkConstPtr plink, std::vector<KinBodyConstPtr> const &vbodyexcluded, std::vector<LinkConstPtr> const &vlinkexcluded, CollisionReportPtr report = CollisionReportPtr())
    {

        if( !plink->IsEnabled() || find(vlinkexcluded.begin(), vlinkexcluded.end(), plink) != vlinkexcluded.end() ) {
            return false;
        }

        _fclspace->Synchronize();


        // TODO : Document/comment
        // TODO : refer to StateSaver/OptionsSaver
        TemporaryManagerAgainstEnvPtr ptmpManagerLinkAgainstEnv = boost::make_shared<TemporaryManagerAgainstEnv>(_fclspace);
        ptmpManagerLinkAgainstEnv->Register(plink);

        std::set<KinBodyPtr> excludedBodies;
        // We exclude the attached bodies here since it would be excluded anyway in NarrowPhaseCheckCollision's step to reduce the number of objects in broad phase collision checking
        KinBodyPtr plinkParent = plink->GetParent();
        plinkParent->GetAttached(excludedBodies);

        FOREACH(itbody, vbodyexcluded) {
            excludedBodies.insert(GetEnv()->GetBodyFromEnvironmentId((*itbody)->GetEnvironmentId()));
        }

        FOREACH(itbody, excludedBodies) {
          // We must not exclude plink so we don't exclude its parent
          if( *itbody != plinkParent ) {
            ptmpManagerLinkAgainstEnv->Exclude(*itbody);
          }
        }

        // We exclude all the links of plink's parent but plink
        for(size_t i = 0; i < plinkParent->GetLinks().size(); ++i) {
          if( i != plink->GetIndex() ) {
            ptmpManagerLinkAgainstEnv->Exclude(plinkParent->GetLinks()[i]);
          }
        }

        FOREACH(itlink, vlinkexcluded) {
            if( !excludedBodies.count((*itlink)->GetParent()) ) {
                ptmpManagerLinkAgainstEnv->Exclude(*itlink);
            }
        }


        if( _options & OpenRAVE::CO_Distance ) {
            return false;
        } else {
            CollisionCallbackDataPtr pquery = boost::make_shared<CollisionCallbackData>(shared_checker(), report);
            ptmpManagerLinkAgainstEnv->Collide(pquery);
            return pquery->_bCollision;
        }
    }

    virtual bool CheckCollision(KinBodyConstPtr pbody, std::vector<KinBodyConstPtr> const &vbodyexcluded, std::vector<LinkConstPtr> const &vlinkexcluded, CollisionReportPtr report = CollisionReportPtr())
    {
        if( (pbody->GetLinks().size() == 0) || !pbody->IsEnabled() ) {
            return false;
        }

        _fclspace->Synchronize();

        TemporaryManagerAgainstEnvPtr ptmpManagerBodyAgainstEnv = boost::make_shared<TemporaryManagerAgainstEnv>(_fclspace);
        // Rename the method or put the code directly here
        FillTemporaryManagerAgainstEnvWithBody(pbody, ptmpManagerBodyAgainstEnv, !!(_options & OpenRAVE::CO_ActiveDOFs), vbodyexcluded, vlinkexcluded);


        if( _options & OpenRAVE::CO_Distance ) {
            RAVELOG_WARN("fcl doesn't support CO_Distance yet\n");
            return false; // TODO
        } else {
            CollisionCallbackDataPtr pquery = boost::make_shared<CollisionCallbackData>(shared_checker(), report);
            ptmpManagerBodyAgainstEnv->Collide(pquery);
            return pquery->_bCollision;
        }
    }

    virtual bool CheckCollision(RAY const &ray, LinkConstPtr plink,CollisionReportPtr report = CollisionReportPtr())
    {
        RAVELOG_WARN("fcl doesn't support Ray collisions\n");
        return false; //TODO
    }

    virtual bool CheckCollision(RAY const &ray, KinBodyConstPtr pbody, CollisionReportPtr report = CollisionReportPtr())
    {
        RAVELOG_WARN("fcl doesn't support Ray collisions\n");
        return false; //TODO
    }

    virtual bool CheckCollision(RAY const &ray, CollisionReportPtr report = CollisionReportPtr())
    {
        RAVELOG_WARN("fcl doesn't support Ray collisions\n");
        return false; //TODO
    }

    virtual bool CheckStandaloneSelfCollision(KinBodyConstPtr pbody, CollisionReportPtr report = CollisionReportPtr())
    {
        if( pbody->GetLinks().size() <= 1 ) {
            return false;
        }

        int adjacentOptions = KinBody::AO_Enabled;
        if( (_options & OpenRAVE::CO_ActiveDOFs) && pbody->IsRobot() ) {
            adjacentOptions |= KinBody::AO_ActiveDOFs;
        }

        const std::set<int> &nonadjacent = pbody->GetNonAdjacentLinks(adjacentOptions);
        _fclspace->Synchronize(pbody);


        std::vector< CollisionGroup > vlinkCollisionGroups(pbody->GetLinks().size());

        if( _options & OpenRAVE::CO_Distance ) {
            RAVELOG_WARN("fcl doesn't support CO_Distance yet\n");
            return false; // TODO
        } else {
            CollisionCallbackDataPtr pquery = boost::make_shared<CollisionCallbackData>(shared_checker(), report);
            pquery->bselfCollision = true;

            FOREACH(itset, nonadjacent) {
                size_t index1 = *itset&0xffff, index2 = *itset>>16;
                LinkConstPtr plink1(pbody->GetLinks().at(index1)), plink2(pbody->GetLinks().at(index2));
                BOOST_ASSERT( plink1->IsEnabled() && plink2->IsEnabled() );
                if( vlinkCollisionGroups.at(index1).empty() ) {
                    _fclspace->GetLinkManager(pbody, index1)->getObjects(vlinkCollisionGroups[index1]);
                }
                if( vlinkCollisionGroups.at(index2).empty() ) {
                    _fclspace->GetLinkManager(pbody, index2)->getObjects(vlinkCollisionGroups[index2]);
                }

                FOREACH(ito1, vlinkCollisionGroups[index1]) {
                    FOREACH(ito2, vlinkCollisionGroups[index2]) {
                        // TODO : consider using the link BV
                        NarrowPhaseCheckGeomCollision(*ito1, *ito2, pquery.get());
                        if( pquery->bstop ) {
                            return pquery->_bCollision;
                        }
                    }
                }
            }
            return pquery->_bCollision;
        }
    }

    virtual bool CheckStandaloneSelfCollision(LinkConstPtr plink, CollisionReportPtr report = CollisionReportPtr())
    {
        KinBodyPtr pbody = plink->GetParent();
        if( pbody->GetLinks().size() <= 1 ) {
            return false;
        }

        int adjacentOptions = KinBody::AO_Enabled;
        if( (_options & OpenRAVE::CO_ActiveDOFs) && pbody->IsRobot() ) {
            adjacentOptions |= KinBody::AO_ActiveDOFs;
        }

        const std::set<int> &nonadjacent = pbody->GetNonAdjacentLinks(adjacentOptions);

        _fclspace->Synchronize(pbody);

        CollisionGroup bodyGroup;

        FOREACH(itset, nonadjacent) {
            KinBody::LinkConstPtr plink1(pbody->GetLinks().at(*itset&0xffff)), plink2(pbody->GetLinks().at(*itset>>16));
            if( plink == plink1 ) {
                _fclspace->GetCollisionObjects(plink2, bodyGroup);
            } else if( plink == plink2 ) {
                _fclspace->GetCollisionObjects(plink1, bodyGroup);
            }
        }

        BroadPhaseCollisionManagerPtr linkManager = _fclspace->GetLinkManager(plink), bodyManager = _fclspace->CreateManager();
        bodyManager->registerObjects(bodyGroup);

        linkManager->setup();
        bodyManager->setup();

        if( _options & OpenRAVE::CO_Distance ) {
            RAVELOG_WARN("fcl doesn't support CO_Distance yet\n");
            return false; //TODO
        } else {
            CollisionCallbackDataPtr pquery = boost::make_shared<CollisionCallbackData>(shared_checker(), report);
            pquery->bselfCollision = true;
            // TODO : consider using the link BV
            linkManager->collide(bodyManager.get(), pquery.get(), &NarrowPhaseCheckGeomCollision);
            return pquery->_bCollision;
        }
    }

private:
    inline boost::shared_ptr<FCLCollisionChecker> shared_checker() {
        return boost::dynamic_pointer_cast<FCLCollisionChecker>(shared_from_this());
    }


    boost::shared_ptr<CollisionCallbackData> SetupDistanceQuery(CollisionReportPtr report)
    {
        // TODO
        return boost::make_shared<CollisionCallbackData>(shared_checker(), report);
    }

    /// \param pbody KinBody whose collision objects are collected
    /// \param ptmpManagerBodyAgainstEnv temporary manager to be filled with the collision objects
    /// \param bactiveDOFs whether only activeDOFs should be checked
    /// \param vbodyexcluded the bodies which should not take part in the collision checking
    /// \param vlinkexcluded the link which should not take part in the collision checking
    ///
    /// Fill ptmpManagerBodyAgainstEnv with the collision objects underlying pbody excluding the collision objects
    /// from vbodyexcluded, vlinkexcluded and also the non-active links of pbody if bactiveDOFs is set
    void FillTemporaryManagerAgainstEnvWithBody(KinBodyConstPtr pbody, TemporaryManagerAgainstEnvPtr ptmpManagerBodyAgainstEnv, bool bactiveDOFs, std::vector<KinBodyConstPtr> const &vbodyexcluded, std::vector<LinkConstPtr> const &vlinkexcluded)
    {

        bool bActiveLinksOnly = false;
        if( bactiveDOFs && pbody->IsRobot() ) {

            RobotBaseConstPtr probot = OpenRAVE::RaveInterfaceConstCast<RobotBase>(pbody);
            bActiveLinksOnly = !probot->GetAffineDOF();

            if( bActiveLinksOnly ) {
                std::vector<int> vactiveLinks = std::vector<int>(probot->GetLinks().size(), false);
                for(size_t i = 0; i < probot->GetLinks().size(); ++i) {
                    int isLinkActive = 0;
                    LinkConstPtr plink = pbody->GetLinks()[i];
                    FOREACH(itindex, probot->GetActiveDOFIndices()) {
                      if( probot->DoesAffect(probot->GetJointFromDOFIndex(*itindex)->GetJointIndex(), i) ) {
                        isLinkActive = 1;
                        break;
                      }
                    }
                    if( isLinkActive && find(vlinkexcluded.begin(), vlinkexcluded.end(), plink) == vlinkexcluded.end() ) {
                        ptmpManagerBodyAgainstEnv->Register(plink);
                    } else {
                        ptmpManagerBodyAgainstEnv->Exclude(plink);
                    }
                    vactiveLinks[i] = isLinkActive;
                }

                std::set<KinBodyPtr> attachedBodies;
                pbody->GetAttached(attachedBodies);

                FOREACH(itbody, attachedBodies) {
                    if(*itbody == pbody) {
                        continue;
                    }
                    if( find(vbodyexcluded.begin(), vbodyexcluded.end(), *itbody) == vbodyexcluded.end() ) {
                        KinBody::LinkPtr pgrabbinglink = probot->IsGrabbing(*itbody);
                        if( !!pgrabbinglink && vactiveLinks[pgrabbinglink->GetIndex()]) {
                            if( vlinkexcluded.size() == 0 ) {
                                ptmpManagerBodyAgainstEnv->Register(*itbody);
                            } else {
                                FOREACH(itlink, (*itbody)->GetLinks()) {
                                  if( find(vlinkexcluded.begin(), vlinkexcluded.end(), *itlink) == vlinkexcluded.end() ) {
                                        ptmpManagerBodyAgainstEnv->Register(*itlink);
                                    }
                                }
                            }
                        } else {
                            // TODO : Check if a body grabbed by a non-active link should be excluded or not
                            ptmpManagerBodyAgainstEnv->Exclude(*itbody);
                        }
                    }
                }
            }
        }

        if( !bActiveLinksOnly ) {
            std::set<KinBodyPtr> attachedBodies;
            pbody->GetAttached(attachedBodies);

            if( vlinkexcluded.size() == 0 ) {
                FOREACH(itbody, attachedBodies) {
                  if( find(vbodyexcluded.begin(), vbodyexcluded.end(), *itbody) == vbodyexcluded.end() ) {
                        ptmpManagerBodyAgainstEnv->Register(*itbody);
                    }
                }
            } else {
                FOREACH(itbody, attachedBodies) {
                  if( find(vbodyexcluded.begin(), vbodyexcluded.end(), *itbody) == vbodyexcluded.end() ) {
                        FOREACH(itlink, (*itbody)->GetLinks()) {
                          if( find(vlinkexcluded.begin(), vlinkexcluded.end(), *itlink) == vlinkexcluded.end() ) {
                                ptmpManagerBodyAgainstEnv->Register(*itlink);
                            }
                        }
                    }
                }
            }
        }

        FOREACH(itbody, vbodyexcluded) {
            ptmpManagerBodyAgainstEnv->Exclude(*itbody);
        }
        FOREACH(itlink, vlinkexcluded) {
            ptmpManagerBodyAgainstEnv->Exclude(*itlink);
        }
    }


    /// \param pbody the KinBody whose collision objects are collected
    /// \param ptmpManager the temporary manager where these collision objects are gathered
    /// \param bactiveDOFs whether only active links should be checked
    /// \param first whether we are considering the first or the second object managed by ptmpManager
    ///
    void FillTemporaryManagerWithBody(KinBodyConstPtr pbody, TemporaryManagerPtr ptmpManager, bool bactiveDOFs, bool first)
    {

        bool bActiveLinksOnly = false;
        if( bactiveDOFs && pbody->IsRobot() ) {

            RobotBaseConstPtr probot = OpenRAVE::RaveInterfaceConstCast<RobotBase>(pbody);
            bActiveLinksOnly = !probot->GetAffineDOF();

            if( bActiveLinksOnly ) {
                std::vector<bool> vactiveLinks = std::vector<bool>(probot->GetLinks().size(), false);
                for(size_t i = 0; i < probot->GetLinks().size(); ++i) {
                    bool isLinkActive = false;
                    LinkConstPtr plink = pbody->GetLinks()[i];
                    FOREACH(itindex, probot->GetActiveDOFIndices()) {
                        isLinkActive = isLinkActive || probot->DoesAffect(probot->GetJointFromDOFIndex(*itindex)->GetJointIndex(), i);
                    }
                    if( isLinkActive ) {
                        ptmpManager->Register(plink, first);
                    }
                    vactiveLinks[i] = isLinkActive;
                }

                std::set<KinBodyPtr> attachedBodies;
                pbody->GetAttached(attachedBodies);

                FOREACH(itbody, attachedBodies) {
                    if(*itbody == pbody) {
                        continue;
                    }
                    KinBody::LinkPtr pgrabbinglink = probot->IsGrabbing(*itbody);
                    if( !!pgrabbinglink && vactiveLinks[pgrabbinglink->GetIndex()]) {
                        ptmpManager->Register(*itbody, first);
                    }
                }
            }
        }

        if( !bActiveLinksOnly ) {
            std::set<KinBodyPtr> attachedBodies;
            pbody->GetAttached(attachedBodies);
            FOREACH(itbody, attachedBodies) {
                ptmpManager->Register(*itbody, first);
            }
        }
    }

    static bool NarrowPhaseCheckCollision(fcl::CollisionObject *o1, fcl::CollisionObject *o2, void *data) {
        CollisionCallbackData* pcb = static_cast<CollisionCallbackData *>(data);
        return pcb->_pchecker->NarrowPhaseCheckCollision(o1, o2, pcb);
    }

    bool NarrowPhaseCheckCollision(fcl::CollisionObject *o1, fcl::CollisionObject *o2, CollisionCallbackData* pcb) {
        LinkConstPtr plink1 = GetCollisionLink(*o1), plink2 = GetCollisionLink(*o2);

        if( !plink1 || !plink2 ) {
            return false;
        }

        if( !plink1->IsEnabled() || !plink2->IsEnabled() ) {
            return false;
        }

        // Proceed to the next if the links are attached and not enabled
        if( !pcb->bselfCollision && plink1->GetParent()->IsAttached(KinBodyConstPtr(plink2->GetParent())) ) {
            return false;
        }

        if( _fclspace->HasMultipleGeometries(plink1) ) {
            if( _fclspace->HasMultipleGeometries(plink2) ) {
                BroadPhaseCollisionManagerPtr plink1Manager = _fclspace->GetLinkManager(plink1), plink2Manager = _fclspace->GetLinkManager(plink2);
                plink1Manager->setup();
                plink2Manager->setup();
                plink1Manager->collide(plink2Manager.get(), pcb, &FCLCollisionChecker::NarrowPhaseCheckGeomCollision);
            } else {
                BroadPhaseCollisionManagerPtr plink1Manager = _fclspace->GetLinkManager(plink1);
                plink1Manager->setup();
                plink1Manager->collide(o2, pcb, &FCLCollisionChecker::NarrowPhaseCheckGeomCollision);
            }
        } else {
            if( _fclspace->HasMultipleGeometries(plink2) ) {
                BroadPhaseCollisionManagerPtr plink2Manager = _fclspace->GetLinkManager(plink2);
                plink2Manager->setup();
                plink2Manager->collide(o1, pcb, &FCLCollisionChecker::NarrowPhaseCheckGeomCollision);
            } else {
                NarrowPhaseCheckGeomCollision(o1, o2, pcb);
            }
        }

        return pcb->bstop;
    }

    static bool NarrowPhaseCheckGeomCollision(fcl::CollisionObject *o1, fcl::CollisionObject *o2, void *data) {
        CollisionCallbackData* pcb = static_cast<CollisionCallbackData *>(data);
        return pcb->_pchecker->NarrowPhaseCheckGeomCollision(o1, o2, pcb);
    }

    bool NarrowPhaseCheckGeomCollision(fcl::CollisionObject *o1, fcl::CollisionObject *o2, CollisionCallbackData* pcb)
    {
        CollisionReport tmpReport;
        LinkConstPtr plink1 = GetCollisionLink(*o1), plink2 = GetCollisionLink(*o2);

        if( !plink1 || !plink2 ) {
            return false;
        }

        if( !plink1->IsEnabled() || !plink2->IsEnabled() ) {
            return false;
        }

        // Proceed to the next if the links are attached and we are not self colliding
        if( !pcb->bselfCollision && plink1->GetParent()->IsAttached(KinBodyConstPtr(plink2->GetParent())) ) {
            return false;
        }

        pcb->_result.clear();
        size_t numContacts = fcl::collide(o1, o2, pcb->_request, pcb->_result);

        if( numContacts > 0 ) {

            if( !!pcb->_report ) {
                tmpReport.Reset(_options);
                tmpReport.plink1 = plink1;
                tmpReport.plink2 = plink2;

                // TODO : eliminate the contacts points (insertion sort (std::lower) + binary_search ?) duplicated
                // TODO : Collision points are really different from their ode variant
                if( _options & (OpenRAVE::CO_Contacts | OpenRAVE::CO_AllGeometryContacts) ) {
                    tmpReport.contacts.resize(numContacts);
                    for(size_t i = 0; i < numContacts; ++i) {
                        fcl::Contact const &c = pcb->_result.getContact(i);
                        tmpReport.contacts[i] = CollisionReport::CONTACT(ConvertVectorFromFCL(c.pos), ConvertVectorFromFCL(c.normal), c.penetration_depth);
                    }
                }


                if( GetEnv()->HasRegisteredCollisionCallbacks() ) {

                    if(pcb->listcallbacks.size() == 0) {
                        GetEnv()->GetRegisteredCollisionCallbacks(pcb->listcallbacks);
                    }

                    OpenRAVE::CollisionAction action = OpenRAVE::CA_DefaultAction;
                    CollisionReportPtr preport(&tmpReport, OpenRAVE::utils::null_deleter());
                    FOREACH(callback, pcb->listcallbacks) {
                        action = (*callback)(preport, false);
                        if( action == OpenRAVE::CA_Ignore ) {
                            return false;
                        }
                    }
                }

                pcb->_report->plink1 = tmpReport.plink1;
                pcb->_report->plink2 = tmpReport.plink2;
                if(pcb->_report->contacts.size() + numContacts < pcb->_report->contacts.capacity()) {
                    pcb->_report->contacts.reserve(pcb->_report->contacts.capacity() + numContacts); // why capacity instead of size ?
                }
                copy(tmpReport.contacts.begin(), tmpReport.contacts.end(), back_inserter(pcb->_report->contacts));
                // pcb->_report->contacts.swap(_report.contacts); // would be faster but seems just wrong...

                LinkPair linkPair = MakeLinkPair(plink1, plink2);
                if( _options & OpenRAVE::CO_AllLinkCollisions ) {
                    // We maintain vLinkColliding ordered
                    typedef std::vector< std::pair< LinkConstPtr, LinkConstPtr > >::iterator PairIterator;
                    PairIterator end = pcb->_report->vLinkColliding.end(), first = std::lower_bound(pcb->_report->vLinkColliding.begin(), end, linkPair);
                    if( first == end || *first != linkPair ) {
                        pcb->_report->vLinkColliding.insert(first, linkPair);
                    }
                }

                pcb->_bCollision = true;
                pcb->bstop = !((bool)(_options & (OpenRAVE::CO_AllLinkCollisions | OpenRAVE::CO_AllGeometryContacts))); // stop checking collision
                return pcb->bstop;
            }

            pcb->_bCollision = true;
            pcb->bstop = true; // since the report is NULL, there is no reason to continue
            return pcb->bstop;
        }

        return false; // keep checking collision
    }

    static bool NarrowPhaseCheckDistance(fcl::CollisionObject *o1, fcl::CollisionObject *o2, void *data)
    {
        // TODO
        return false;
    }

    static LinkPair MakeLinkPair(LinkConstPtr plink1, LinkConstPtr plink2)
    {
        if( plink1.get() < plink2.get() ) {
            return make_pair(plink1, plink2);
        } else {
            return make_pair(plink1, plink2);
        }
    }

    LinkConstPtr GetCollisionLink(fcl::CollisionObject const &collObj)
    {
        FCLSpace::KinBodyInfo::LINK *link_raw = static_cast<FCLSpace::KinBodyInfo::LINK *>(collObj.getUserData());
        if( link_raw != NULL ) {
            LinkConstPtr plink = link_raw->GetLink();
            if( !plink ) {
                RAVELOG_WARN(str(boost::format("The link %s was lost from fclspace")%link_raw->bodylinkname));
            }
            return plink;
        }
        RAVELOG_WARN("fcl collision object does not have a link attached");
        return LinkConstPtr();
    }



    int _options;
    boost::shared_ptr<FCLSpace> _fclspace;
    int _numMaxContacts;
    std::string _userdatakey;
    bool bisSelfCollisionChecker;
};


#endif