#pragma warning(disable:4786)

#include <cmath>
#include <iostream>

#include "VascularTree.h"
#include "OxygenationMap.h"
#include "NodeTable.h"

#define PI 3.1415926535897

using namespace std;

VascularTree::VascularTree(OxygenationMap * _oxMap, double* _perf, double _Pperf, double _Pterm, double _Qperf, double _row, double _gamma, double _lambda, double _mew, double _minDistance, int _numNodes, double _voxelWidth, int _closestNeighbours){
	oxMap = _oxMap;
	perf = new double[3]; perf[0] = _perf[0]; perf[1] = _perf[1];perf[2] = _perf[2];
	Pperf = _Pperf;
	Pterm = _Pterm;
	Qperf = _Qperf;
	row = _row;
	gamma = _gamma;
	lambda = _lambda;
	mew = _mew;
	minDistance = _minDistance;
	mapVoxelWidth = _voxelWidth;
	Qterm = _Qperf/_numNodes;
	numNodes = _numNodes;
	
	closestNeighbours = _closestNeighbours;
	
	nt.addNode(NodeTable::ROOT, perf, -1, 1, 1, Qperf, -1, -1);
}

//calculate the distance between to nodes in the node table
double VascularTree::distance(int from, int to){
	double* fromPos = nt.getPos(from);
	double* toPos = nt.getPos(to);
	
	return  sqrt(
				pow(fromPos[0] - toPos[0], 2) + 
				pow(fromPos[1] - toPos[1], 2) +
				pow(fromPos[2] - toPos[2], 2))*mapVoxelWidth;
}

//calculate the reduced resistence of segment @ id
void VascularTree::calculateReducedResistence(int id){
	if(nt.getType(id) == NodeTable::TERM){
		double acc = (8.0 * row * distance(id, nt.getParent(id))/PI); 
		nt.setReducedResistence(id, acc);
	} else {
		double acc = 0;
		acc += pow(nt.getLeftRatio(id), 4) / nt.getReducedResistance(nt.getLeftChild(id));
		acc += pow(nt.getRightRatio(id), 4) / nt.getReducedResistance(nt.getRightChild(id));
		acc = 1.0 / acc;
		acc += (8.0 * row * distance(id, nt.getParent(id))/PI);
		nt.setReducedResistence(id, acc);
	}
}

//calculates the ratio of radii of the segment @ id
void VascularTree::calculateRatios(int id){
	int left = nt.getLeftChild(id);
	int right = nt.getRightChild(id);
	
	double left_over_right = (nt.getFlow(left)*nt.getReducedResistance(left)) / (nt.getFlow(right) * nt.getReducedResistance(right));
	left_over_right = pow(left_over_right, 0.25);
	
	nt.setLeftRatio(id, pow((1 + pow(left_over_right, -gamma)), (-1.0)/gamma));
	nt.setRightRatio(id, pow((1 + pow(left_over_right, gamma)), (-1.0)/gamma));
}

//update the tree @ the bifurication point @ id
void VascularTree::updateAtBifurication(int id, int newChild){
	if(nt.getType(id) != NodeTable::ROOT){		
		calculateReducedResistence(newChild);
		calculateRatios(id);
		
		updateAtBifurication(nt.getParent(id), id);
	} else {
		calculateReducedResistence(newChild);
	}
}

//calculate the radii throughout the tree
void VascularTree::calculateRadius(){
	//the child of the root (perferation) node defines the root segment
	int rootChild = nt.getLeftChild(0);
	
	double radius = (nt.getFlow(rootChild) * nt.getReducedResistance(rootChild)) / (Pperf - Pterm);
	radius = pow(radius, 0.25);
	nt.setRadius(rootChild, radius);
	
	calculateRadius(rootChild);
}

void VascularTree::calculateRadius(int id){
	if(nt.getType(id) == NodeTable::TERM)
		return;
	
	int left = nt.getLeftChild(id);
	int right = nt.getRightChild(id);
	
	nt.setRadius(left, nt.getRadius(id)*nt.getLeftRatio(id));
	nt.setRadius(right, nt.getRadius(id)*nt.getRightRatio(id));
	
	calculateRadius(left);
	calculateRadius(right);
}

double VascularTree::calculateFitness(){
	double acc = 0;
	for(int i = 1; i < nt.nodes.size(); i++){
		acc += pow(distance(i, nt.getParent(i)), mew) * pow(nt.getRadius(i), lambda);
	}
	
	return acc;
}

/**
 * When used by local optimization, ignored is the segment to connect to
 * otherwise it sould be -1;
 */
bool VascularTree::validateCandidate(double* x0, int ignored){
	
	for(int i = 1; i < nt.nodes.size(); i++){
		if(i != ignored){
			double distance = pointSegmentDistance(x0, i);
			if(distance < minDistance)
				return false;
		}
	}
	
	return true;
}

//connect point to segment through bifPoint
void VascularTree::connectPoint(double* point, int segment, double* bifPoint){
	if(nt.getType(segment) == NodeTable::ROOT){
		nt.addNode(NodeTable.TERM, point, segment, 1, 1, Qterm, -1, -1);
		nt.setLeftChild(0, 1);
		nt.setRightChild(0, 1);
		calculateReducedResistence(1);
	} else {
		// *--- <I_seg> --- *
		//			
		//			becomes
		//
		//	*--- <I_bif> --- * ---- < I_con > --- *
		//					 \
		//					  \
		//					  <I_new > 
		//						\
		//						 \
		//						  * point[]
		//
		//
		//  where I_sec is replaced with I_con
		//
		//
		// 	I_con = I_seg with the exception of parent (which is set to I_biff
		//  I_seg's parent updates its child to be I_biff
		//  I_new = is a new segment wich is trivially built
		//  I_biff is built using I_new and I_con
		int biffId = nt.nodes.size();
		int newId = biffId + 1;
		
		int oldParent = nt.getParent(segment);
		
		nt.setParent(segment, biffId);
		if(nt.getLeftChild(oldParent) == segment)
			nt.setLeftChild(oldParent, biffId);
		if(nt.getRightChild(oldParent) == segment)
			nt.setRightChild(oldParent, biffId);
		
		if(oldParent > 0)
			incrementFlow(oldParent, Qterm);
		
		nt.addNode(NodeTable.BIF, bifPoint, oldParent, 1, 1, nt.getFlow(segment) + Qterm, segment, newId);
		nt.addNode(NodeTable.TERM, point, biffId, 1, 1, Qterm, -1, -1);
		
		calculateReducedResistence(segment);
		updateAtBifurication(biffId, newId);
	}
}

//update the flow throughout the tree
void VascularTree::incrementFlow(int parent, double Qterm){
	nt.setFlow(parent, nt.getFlow(parent)+Qterm);
	if(nt.getParent(parent) > 0)
		incrementFlow(nt.getParent(parent), Qterm);
}

//the distance between a point and a segment
double VascularTree::pointSegmentDistance(double* x0, int segment){
	double *pos1 = nt.getPos(segment);
	double *pos2 = nt.getPos(nt.getParent(segment));

	double t = -((pos2[0] - pos1[0])*(pos1[0] - x0[0]) + 
				 (pos2[1] - pos1[1])*(pos1[1] - x0[1]) + 
				 (pos2[2] - pos1[2])*(pos1[2] - x0[2])) / 
				((pos2[0] - pos1[0])*(pos2[0] - pos1[0]) + 
				 (pos2[1] - pos1[1])*(pos2[1] - pos1[1]) +
				 (pos2[2] - pos1[2])*(pos2[2] - pos1[2]));
	
	if(t < 0 || t > 1){
		double d1 = pow(pos1[0] - x0[0], 2) + pow(pos1[1] - x0[1], 2) + pow(pos1[2] - x0[2], 2);
		double d2 = pow(pos2[0] - x0[0], 2) + pow(pos2[1] - x0[1], 2) + pow(pos2[2] - x0[2], 2);
		
		if(d1 < d2)
			return pow(d1, 0.5);
		else
			return pow(d2, 0.5);
	}
	else{
		return  pow(pow((((pos2[0] - pos1[0])*t + pos1[0]) - x0[0]), 2) + 
						 pow((((pos2[1] - pos1[1])*t + pos1[1]) - x0[1]), 2) + 
						 pow((((pos2[2] - pos1[2])*t + pos1[2]) - x0[2]), 2), 0.5);
	}
}

//optimize the location of a bifurication point for terminal node point and segment 'segment'
double* VascularTree::localOptimization(double * point, int segment, int steps){
	double bestFitness = 1e200;

	double bif[3];
	double perf[3];
	perf[0] = nt.getPos(nt.getParent(segment))[0];
	perf[1] = nt.getPos(nt.getParent(segment))[1];
	perf[2] = nt.getPos(nt.getParent(segment))[2];
	double con[3];
	con[0] = nt.getPos(segment)[0];
	con[1] = nt.getPos(segment)[1];
	con[2] = nt.getPos(segment)[2];
	
	bif[0] = ((con[0] - perf[0])/2.0 + perf[0]);
	bif[1] = ((con[1] - perf[1])/2.0 + perf[1]);
	bif[2] = ((con[2] - perf[2])/2.0 + perf[2]);
	
	double stepSize = (((perf[0]+con[0]+point[0])/3.0) - bif[0]+
					   ((perf[1]+con[1]+point[1])/3.0) - bif[1] +
					   ((perf[2]+con[2]+point[2])/3.0) - bif[2])* 2.0 / steps;
	
	
	//makesure point is visible from bif - otherwise return null
	if(!oxMap->visible(bif, point) || !inVolume(bif))
		return NULL;
	
	nt.startUndo();

	connectPoint(point, segment, bif);
	nt.applyUndo();
		
	double localBest[3];
	double test[3];

	for(int i = 0; i < steps; i++){
		localBest[0] = bif[0]; localBest[1] = bif[1]; localBest[2] = bif[2];
		//try the neighbours of bif (6 connected) as possible new bif points
		//		for a bif point to be valid:
		//			- perf must be visible from bif
		//			- con must be visible from bif
		//			- point must be visilbe from bif
		//			- bif must be a valid candidate (using segment as the ignored)
		//
		//	if the current bif is ever a local optima -search is terminated
		
		//up
		test[0] = bif[0]+stepSize; test[1] = bif[1]; test[2] = bif[2];
		if(inVolume(test) &&oxMap->visible(perf, test) && oxMap->visible(con, test) && oxMap->visible(point, test) && validateCandidate(test, segment)){
			connectPoint(point, segment, test);
			calculateRadius();
			double fitness = calculateFitness();
			
			if(fitness < bestFitness){
				localBest[0] = test[0]; localBest[1] = test[1]; localBest[2] = test[2];
				bestFitness = fitness;
			}
		}
		nt.applyUndo();
		
		//down
		test[0] = bif[0]-stepSize; test[1] = bif[1]; test[2] = bif[2];
		if(inVolume(test) &&oxMap->visible(perf, test) && oxMap->visible(con, test) && oxMap->visible(point, test) && validateCandidate(test, segment)){
			connectPoint(point, segment, test);
			calculateRadius();
			double fitness = calculateFitness();
			
			if(fitness < bestFitness){
				localBest[0] = test[0]; localBest[1] = test[1]; localBest[2] = test[2];
				bestFitness = fitness;
			}
		}
		nt.applyUndo();

		//left
		test[0] = bif[0]; test[1] = bif[1]+stepSize; test[2] = bif[2];
		if(inVolume(test) &&oxMap->visible(perf, test) && oxMap->visible(con, test) && oxMap->visible(point, test) && validateCandidate(test, segment)){
			connectPoint(point, segment, test);
			calculateRadius();
			double fitness = calculateFitness();
			
			if(fitness < bestFitness){
				localBest[0] = test[0]; localBest[1] = test[1]; localBest[2] = test[2];
				bestFitness = fitness;
			}
		}
		nt.applyUndo();

		//right
		test[0] = bif[0]; test[1] = bif[1]-stepSize; test[2] = bif[2];
		if(inVolume(test) &&oxMap->visible(perf, test) && oxMap->visible(con, test) && oxMap->visible(point, test) && validateCandidate(test, segment)){
			connectPoint(point, segment, test);
			calculateRadius();
			double fitness = calculateFitness();
			
			if(fitness < bestFitness){
				localBest[0] = test[0]; localBest[1] = test[1]; localBest[2] = test[2];
				bestFitness = fitness;
			}
		}
		nt.applyUndo();

		//forward
		test[0] = bif[0]; test[1] = bif[1]; test[2] = bif[2]+stepSize;
		if(inVolume(test) &&oxMap->visible(perf, test) && oxMap->visible(con, test) && oxMap->visible(point, test) && validateCandidate(test, segment)){
			connectPoint(point, segment, test);
			calculateRadius();
			double fitness = calculateFitness();
			
			if(fitness < bestFitness){
				localBest[0] = test[0]; localBest[1] = test[1]; localBest[2] = test[2];
				bestFitness = fitness;
			}
		}
		nt.applyUndo();

		//back
		test[0] = bif[0]; test[1] = bif[1]; test[2] = bif[2]-stepSize;
		if(inVolume(test) &&oxMap->visible(perf, test) && oxMap->visible(con, test) && oxMap->visible(point, test) && validateCandidate(test, segment)){
			connectPoint(point, segment, test);
			calculateRadius();
			double fitness = calculateFitness();
			
			if(fitness < bestFitness){
				localBest[0] = test[0]; localBest[1] = test[1]; localBest[2] = test[2];
				bestFitness = fitness;
			}
		}
		nt.applyUndo();
		
		if(localBest[0] != bif[0] || localBest[1] != bif[1] || localBest[2] != bif[2]){
			bif[0] = localBest[0]; bif[1] = localBest[1]; bif[2] = localBest[2];
		} else {
			break;
		}
	}
	nt.clearUndo();
	nt.stopUndo();
	
	double *ret = new double[4];
	ret[0] = bif[0]; ret[1] = bif[1]; ret[2] = bif[2];
	ret[3] = bestFitness;
	return ret;
}

//determine if point is in the volume
bool VascularTree::inVolume(double* point){
	if(point[0] < 0 || point[0] >= oxMap->dim[0])
		return false;
	if(point[1] < 0 || point[1] >= oxMap->dim[1])
		return false;
	if(point[2] < 0 || point[2] >= oxMap->dim[2])
		return false;
	
	return true;
}

//try to attach point to the tree
bool VascularTree::connectCandidate(double* point, int steps){
	if(!validateCandidate(point, -1))
		return false;	//candiate is too close to an existing segment
	
	if(nt.nodes.size() == 1){
		if(!oxMap->visible(nt.getPos(0), point))
			return false;
		connectPoint(point, 0, NULL);
		return true;
	}
	
	double best[3];
	bool foundSolution = false;
	double bestFitness = 1e200;
	int bestSegment = 0;
	
	map<int, double> distances;
	int i, j;
	
	//determine the distance between the point and each segment
	for(i = 1; i < nt.nodes.size(); i++){
		double d = pointSegmentDistance(point, i);
		distances[i] = d;
	}
	
	int numCandidates = distances.size();
	int *candidateSegments = new int[numCandidates];
	
	//sort the segments by distance
	for(j = 0; j < numCandidates; j++){
		int closest = -1;
		double distance = 1e200;
		
		map<int, double>::iterator itr;
		for(itr = distances.begin(); itr != distances.end(); itr++){
			double d = itr->second;
			if(d < distance){
				distance = d;
				closest = itr->first;
			}
		}
		
		if(closest >= 1)
			distances.erase(closest);
		if(closest == -1)
			return false;
		candidateSegments[j] = closest;
	}
	
	//try the first 'closestNeighbours' segments
	int count = 0;
	double* test;
	for(j = 0; j < numCandidates && count < closestNeighbours; j++){
		test = localOptimization(point, candidateSegments[j], steps);
		if(test != NULL){
			count++;
			if(test[3] < bestFitness){
				best[0] = test[0]; best[1] = test[1]; best[2] = test[2];
				bestSegment = candidateSegments[j];
				bestFitness = test[3];
				foundSolution = true;
			}
		}
		delete test;
	}
	
	delete candidateSegments;

	if(!foundSolution)
		return false; //could not connect candidate to ANY segment
	
	connectPoint(point, bestSegment, best);
	
	return true;
}

//build the tree
void VascularTree::buildTree(){
	int count = 0;
	double cand[3];
	int term[3];
	while(count < numNodes){
		double sum = oxMap->sum();
		oxMap->candidate(sum, term);
				
		cand[0] = term[0]; cand[1] = term[1]; cand[2] = term[2];
		if(connectCandidate(cand, 20)){
			count++;
			oxMap->applyCandidate(term);
		}
	}
	
	calculateRadius();
}