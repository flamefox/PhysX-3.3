/*
 * Copyright (c) 2008-2015, NVIDIA CORPORATION.  All rights reserved.
 *
 * NVIDIA CORPORATION and its licensors retain all intellectual property
 * and proprietary rights in and to this software, related documentation
 * and any modifications thereto.  Any use, reproduction, disclosure or
 * distribution of this software and related documentation without an express
 * license agreement from NVIDIA CORPORATION is strictly prohibited.
 */
// Copyright (c) 2004-2008 AGEIA Technologies, Inc. All rights reserved.
// Copyright (c) 2001-2004 NovodeX AG. All rights reserved.  


#include "GuVecConvexHull.h"
#include "GuGeometryUnion.h"

#include "GuContactMethodImpl.h"
#include "GuContactBuffer.h"

#ifdef	PCM_LOW_LEVEL_DEBUG
#include "CmRenderOutput.h"
extern physx::Cm::RenderOutput* gRenderOutPut;
#endif

using namespace physx;
using namespace Gu;


namespace physx
{
namespace Gu
{

bool pcmContactPlaneConvex(GU_CONTACT_METHOD_ARGS)
{
	PX_UNUSED(shape0);
	using namespace Ps::aos;

	Gu::PersistentContactManifold& manifold = cache.getManifold();
	Ps::prefetchLine(&manifold, 256);

	// Get actual shape data
	const PxConvexMeshGeometryLL& shapeConvex = shape1.get<const PxConvexMeshGeometryLL>();

	const PsTransformV transf0 = loadTransformA(transform1);//convex transform
	const PsTransformV transf1 = loadTransformA(transform0);//plane transform
	//convex to plane
	const PsTransformV curTransf(transf1.transformInv(transf0));
	
	const Vec3V vScale = V3LoadU(shapeConvex.scale.scale);
	const Gu::ConvexHullData* hullData = shapeConvex.hullData;
	const FloatV convexMargin = Gu::CalculatePCMConvexMargin(hullData, vScale);
	

	//in world space
	const Vec3V planeNormal = V3Normalize(QuatGetBasisVector0(transf1.q));
	const Vec3V negPlaneNormal = V3Neg(planeNormal);
	

	const FloatV contactDist = FLoad(contactDistance);

	//const FloatV replaceBreakingThreshold = FMul(convexMargin, FLoad(0.001f));
	const FloatV projectBreakingThreshold = FMul(convexMargin, FLoad(0.2f));
	const PxU32 initialContacts = manifold.mNumContacts;
	
	manifold.refreshContactPoints(curTransf, projectBreakingThreshold, contactDist);

	const PxU32 newContacts = manifold.mNumContacts;
	const bool bLostContacts = (newContacts != initialContacts);//((initialContacts == 0) || (newContacts != initialContacts));

	
	PX_UNUSED(bLostContacts);
	//if(bLostContacts || manifold.invalidate_BoxConvex(curTransf, convexMargin))
	if(bLostContacts || manifold.invalidate_PrimitivesPlane(curTransf, convexMargin, FLoad(0.2f)))
	{
		const PsMatTransformV aToB(curTransf);
		const QuatV vQuat = QuatVLoadU(&shapeConvex.scale.rotation.x);

		const Mat33V vertex2Shape = ConstructVertex2ShapeMatrix(vScale, vQuat);
		
		//ML:localNormal is the local space of plane normal, however, because shape1 is box and shape0 is plane, we need to use the reverse of contact normal(which will be the plane normal) to make the refreshContactPoints
		//work out the correct pentration for points
		const Vec3V localNormal = V3UnitX();

		manifold.mNumContacts = 0;
		manifold.setRelativeTransform(curTransf);
		const PxVec3* __restrict verts = hullData->getHullVertices();
		const PxU8 numVerts = hullData->mNbHullVertices;

		Gu::PersistentContact* manifoldContacts = PX_CP_TO_PCP(contactBuffer.contacts);
		PxU32 numContacts = 0;
		
		const PsMatTransformV aToBVertexSpace(aToB.p, M33MulM33(aToB.rot, vertex2Shape));
		//brute force each points
		for(PxU8 i=0; i<numVerts; ++i)
		{
			//in the vertex space of convex
			const Vec3V pInVertexSpace = V3LoadU(verts[i]);
			
			//transform p into plane space
			const Vec3V pInPlaneSpace = aToBVertexSpace.transform(pInVertexSpace);//V3Add(aToB.p, M33MulV3(temp1, pInVertexSpace));
		
			const FloatV signDist = V3GetX(pInPlaneSpace);
			
			if(FAllGrtr(contactDist, signDist))
			{
				//transform p into shape space
				const Vec3V pInShapeSpace = M33MulV3(vertex2Shape, pInVertexSpace);
				//add to manifold
				manifoldContacts[numContacts].mLocalPointA = pInShapeSpace;
				manifoldContacts[numContacts].mLocalPointB = V3NegScaleSub(localNormal, signDist, pInPlaneSpace); 
				manifoldContacts[numContacts++].mLocalNormalPen = V4SetW(Vec4V_From_Vec3V(localNormal), signDist);
			}
		}

		//reduce contacts
		manifold.addBatchManifoldContacts(manifoldContacts, numContacts);
		manifold.addManifoldContactsToContactBuffer(contactBuffer, negPlaneNormal, transf1);

		return manifold.getNumContacts() > 0;
	}
	else
	{
		manifold.addManifoldContactsToContactBuffer(contactBuffer, negPlaneNormal, transf1);
		return manifold.getNumContacts() > 0;
	}
	
}

}//Gu
}//physx
