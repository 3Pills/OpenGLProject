#include "PhysScene.h"
#include "FBXModel.h"
#include <Gizmos.h>

#define Assert(val) if (val){}else{ *((char*)0) = 0;}
#define ArrayCount(val) (sizeof(val)/sizeof(val[0]))

class MyAllocator : public physx::PxAllocatorCallback{
	virtual void* allocate(size_t size, const char* typeName, const char* filename, int line){
		void* new_mem = malloc(size + 32);
		void* result = (char*)new_mem + (16 - ((size_t)new_mem % 16));
		Assert(((size_t)result % 16) == 0);
		*(void**)result = new_mem;
		return (char*)result + 16;
	}

	virtual void deallocate(void* ptr){
		if (ptr){
			void* real_ptr = *(void**)((char*)ptr - 16);
			free(real_ptr);
		}
	}
};

PhysScene::PhysScene() {
	PxAllocatorCallback *myCallback = new MyAllocator();

	m_physicsFoundation = PxCreateFoundation(PX_PHYSICS_VERSION, *myCallback, m_defaultErrorCallback);
	m_physics = PxCreatePhysics(PX_PHYSICS_VERSION, *m_physicsFoundation, PxTolerancesScale());

	PxInitExtensions(*m_physics);

	PxSceneDesc sceneDesc(m_physics->getTolerancesScale());
	sceneDesc.gravity = PxVec3(0, -20.0f, 0);
	sceneDesc.filterShader = &PxDefaultSimulationFilterShader;
	sceneDesc.cpuDispatcher = PxDefaultCpuDispatcherCreate(1);

	m_physicsScene = m_physics->createScene(sceneDesc);

	PxTolerancesScale toleranceScale;
	toleranceScale.mass = 1000;
	toleranceScale.speed = sceneDesc.gravity.y;
	bool value = toleranceScale.isValid(); // make sure this value is always true

	if (value) {
		m_cooking = PxCreateCooking(PX_PHYSICS_VERSION, *m_physicsFoundation, PxCookingParams(toleranceScale));
	}

	//Create a plane below everything to catch stray physics objects.
	m_planePose = PxTransform(PxVec3(0.f, 0.f, 0.f), PxQuat(PxHalfPi * 1.0f, PxVec3(0.0f, 0.0f, 1.0f)));
	m_plane = PxCreateStatic(*m_physics, m_planePose, PxPlaneGeometry(), *m_physics->createMaterial(1.f, 1.f, 1.f));
	m_physicsScene->addActor(*m_plane);

	if (m_physics->getPvdConnectionManager() != NULL)
		m_debuggerConnection = PxVisualDebuggerExt::createConnection(m_physics->getPvdConnectionManager(), "127.0.0.1", 5425, 100, PxVisualDebuggerExt::getAllConnectionFlags());
}

PhysScene::~PhysScene() {
	if (m_debuggerConnection != nullptr)
		m_debuggerConnection->release();
	m_physics->release();
}

void PhysScene::Update(float dt, bool a_renderGizmos) {
	float dtClamped = (dt > 1.0f / 30.f) ? 1.0f / 30.0f : dt;
	m_physicsScene->simulate(dtClamped);
	while (m_physicsScene->fetchResults() == false)	{}


	int numberActors = m_physicsScene->getNbActors(PxActorTypeFlags(PxActorTypeFlag::eRIGID_DYNAMIC));
	PxActor** dynamicActors = new PxActor*[numberActors] {};
	int actorCount = m_physicsScene->getActors(PxActorTypeFlags(PxActorTypeFlag::eRIGID_DYNAMIC), dynamicActors, numberActors);

	for (int i = 0; i < actorCount; i++) {
		PxRigidActor* actor = (PxRigidActor*)dynamicActors[i];
		PxTransform transform = actor->getGlobalPose();

		vec3 globalPos = (*(vec3*)(&transform.p[0]));
		quat globalRot = quat(transform.q.w, transform.q.x, transform.q.y, transform.q.z);

		if (dynamicActors[i]->userData != nullptr) {
			FBXModel* model = ((FBXModel*)actor->userData);

			model->m_pos = globalPos;
			model->m_rot = globalRot;
		}

		if (a_renderGizmos) {
			int numberShapes = actor->getNbShapes();
			PxShape** shapes = new PxShape*[numberShapes] {};
			int shapeCount = actor->getShapes(shapes, numberShapes);

			for (int i = 0; i < shapeCount; i++) {
				PxTransform shapeTransform = shapes[i]->getLocalPose();
				vec3 localPos = (*(vec3*)(&shapeTransform.p[0]));
				quat localRot = quat(shapeTransform.q.w, shapeTransform.q.x, shapeTransform.q.y, shapeTransform.q.z);

				//Get the matrix of the local position, rotated by the local and global rotation matrices.
				mat4 localMat = glm::toMat4(globalRot) * glm::toMat4(localRot) * glm::translate(localPos);
				localPos = localMat[3].xyz; //Assign it back to the local pos.

				switch (shapes[i]->getGeometryType()){
				case PxGeometryType::eSPHERE:
					Gizmos::addSphere(globalPos + localPos, shapes[i]->getGeometry().sphere().radius, 12, 12, vec4(0, 1, 0, 1), &localMat);
					break;
				case PxGeometryType::eCAPSULE:
					Gizmos::addCapsule(globalPos + localPos, shapes[i]->getGeometry().capsule().halfHeight * 2, shapes[i]->getGeometry().capsule().radius, 12, 12, vec4(0, 1, 0, 1), &localMat);
					break;
				case PxGeometryType::eBOX:
					Gizmos::addAABB(globalPos + localPos, (*(vec3*)(&shapes[i]->getGeometry().box().halfExtents[0])), vec4(0, 1, 0, 1), &localMat);
					break;
				}
			}
			delete[] shapes;
		}
	}
	delete[] dynamicActors;
}

void PhysScene::AddRigidBodyStatic(PxTransform a_transform, PxGeometry* a_geometry, PxMaterial* a_physMaterial, void* a_userData, float a_density){
	PxRigidDynamic* m_phys = PxCreateDynamic(*m_physics, a_transform, *a_geometry, *a_physMaterial, a_density);
	m_phys->userData = a_userData;

	m_physicsScene->addActor(*m_phys);
}

void PhysScene::AddRigidBodyDynamic(PxTransform a_transform, PxGeometry* a_geometry, PxMaterial* a_physMaterial, void* a_userData, float a_density){
	PxRigidDynamic* m_phys = PxCreateDynamic(*m_physics, a_transform, *a_geometry, *a_physMaterial, a_density);

	//Align the PhysX with the FBXModel origin.
	if (a_userData != nullptr) {
		m_phys->userData = a_userData;
		FBXModel* model = ((FBXModel*)a_userData);

		PxGeometryHolder* geometry = new PxGeometryHolder();
		geometry->storeAny(*a_geometry);

		int numberShapes = m_phys->getNbShapes();
		PxShape* shapes;
		m_phys->getShapes(&shapes, numberShapes);

		vec3 modelExtents = vec3(0);
		switch (geometry->getType()) {
		case PxGeometryType::eSPHERE:
			modelExtents = vec3(0, geometry->sphere().radius, 0);
			break;
		case PxGeometryType::eCAPSULE:
			modelExtents = vec3(geometry->capsule().halfHeight, 0, 0);

			//Capsules are sideways by default, need to rotate soulspear to follow that.
			model->m_modTransform = (glm::rotate(glm::radians(90.0f), vec3(0, 0, 1))) * model->m_modTransform;
			break;
		case PxGeometryType::eBOX:
			modelExtents = vec3(0, geometry->box().halfExtents.y, 0);
		}

		PxTransform relativePose = PxTransform((*(PxVec3*)(&modelExtents[0])));
		shapes->setLocalPose(relativePose);

		delete geometry;
	}

	m_physicsScene->addActor(*m_phys);
}

void PhysScene::AttachRigidBodyConvex(PxTransform a_transform, PxMaterial* a_physMaterial, void* a_userData, float a_density, float a_physModelScale) {
	if (a_userData == nullptr)
		return;

	PxRigidDynamic* m_phys = PxCreateDynamic(*m_physics, a_transform, PxBoxGeometry(1, 1, 1), *a_physMaterial, a_density);

	m_phys->userData = a_userData;
	int numberVerts = 0;
	FBXModel* model = ((FBXModel*)a_userData);

	//find out how many verts there are in total in model
	for (unsigned int i = 0; i < model->m_file->getMeshCount(); ++i) {
		FBXMeshNode* mesh = model->m_file->getMeshByIndex(i);
		numberVerts += mesh->m_vertices.size();
	}

	//reserve space for vert buffer
	PxVec3 *verts = new PxVec3[numberVerts];
	int vertIDX = 0;

	//copy our verts from all the sub meshes and tranform them into the same space
	for (unsigned int i = 0; i < model->m_file->getMeshCount(); ++i){
		FBXMeshNode* mesh = model->m_file->getMeshByIndex(i);
		int numberVerts = mesh->m_vertices.size();
		for (int vertCount = 0; vertCount< numberVerts; vertCount++){
			//Scale the global transform by an arbitrary number, based on what the model individually requires.
			glm::vec4 temp = mesh->m_globalTransform * glm::scale(vec3(a_physModelScale)) * mesh->m_vertices[vertCount].position;
			verts[vertIDX++] = PxVec3(temp.x, temp.y, temp.z);
		}
	}

	PxConvexMeshDesc convexDesc;
	convexDesc.points.count = numberVerts;
	convexDesc.points.stride = sizeof(PxVec3);
	convexDesc.points.data = verts;
	convexDesc.flags = PxConvexFlag::eCOMPUTE_CONVEX;
	convexDesc.vertexLimit = 128;

	PxDefaultMemoryOutputStream* buf = new PxDefaultMemoryOutputStream();
	bool succeeded = false;
	if (m_cooking != nullptr) {
		succeeded = m_cooking->cookConvexMesh(convexDesc, *buf);
	}

	PxU8* contents = buf->getData();
	PxU32 size = buf->getSize();
	PxDefaultMemoryInputData input(contents, size);
	PxConvexMesh* convexMesh = m_physics->createConvexMesh(input);
	PxTransform pose = PxTransform(PxVec3(0.0f));
	PxShape* convexShape = m_phys->createShape(PxConvexMeshGeometry(convexMesh), *a_physMaterial, pose);

	//remove the placeholder box we started with
	int numberShapes = m_phys->getNbShapes();
	PxShape** shapes = (PxShape**)_aligned_malloc(sizeof(PxShape*)*numberShapes, 16);
	m_phys->getShapes(shapes, numberShapes);
	m_phys->detachShape(**shapes);

	delete(verts); //delete our temporary vert buffer.

	m_physicsScene->addActor(*m_phys);
}