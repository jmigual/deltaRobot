﻿using UnityEngine;
using System.Collections;

public class CameraController : MonoBehaviour {

	public GameObject center;
	public bool centered = true;
	public float distance=10f;
	public float distanceMin=3f;
	public float distanceMax=30f;
	public float zoomspeed=500f;
	public float theta;
	public float phi;
	public float cameraspeed=2000f;
	public float camRayLength=100f;
	public float objectiveSwitchSpeed=10f;
	public float dragSpeed=10f;
	public float movementSpeed=20f;
	public float size = 5f;

	public static bool onlyZoom = false;

	
	private Transform ObjectiveInitial;
	private Vector3 obj_pos;
	private const float rad=2*Mathf.PI/360;
	private Vector3 ObjectiveDesired;
	private Vector2 LastMouse;
	private bool switchingobjective=false;

	private float desiredDistance;

	void Start () {
		desiredDistance=distance;
		ObjectiveInitial=center.transform;
		obj_pos = ObjectiveInitial.position;
		ObjectiveDesired=obj_pos;
	}

	void Update () {
		if (!onlyZoom) update_objective ();
		update_angles ();
		if (!onlyZoom) update_transform();
	}


	void update_objective (){
		if (Input.GetKeyDown (KeyCode.Mouse2)){
			Ray CamRay = Camera.main.ScreenPointToRay(Input.mousePosition);
			RaycastHit floorHit;
			if (Physics.Raycast (CamRay, out floorHit, camRayLength, 1)) {
				switchingobjective=true;
				centered=true;
				center=floorHit.collider.gameObject;
			}
		}
		if (Input.GetKey(KeyCode.A)){
			if (centered){
				centered=false;
				switchingobjective=false;
				ObjectiveDesired=obj_pos;
			}
			Vector3 desp=new Vector3 (Mathf.Sin(theta*rad),0,-Mathf.Cos(theta*rad));
			obj_pos+=desp*movementSpeed*Time.fixedDeltaTime;
		} 
		if (Input.GetKey(KeyCode.D)){
			if (centered){
				centered=false;
				switchingobjective=false;
				ObjectiveDesired=obj_pos;
			}
			Vector3 desp=new Vector3 (-Mathf.Sin(theta*rad),0,Mathf.Cos(theta*rad));
			obj_pos+=desp*movementSpeed*Time.fixedDeltaTime;
		} 
		if (Input.GetKey(KeyCode.W)){
			if (centered){
				centered=false;
				switchingobjective=false;
				ObjectiveDesired=obj_pos;
			}
			Vector3 desp=new Vector3 (-Mathf.Cos(theta*rad),0,-Mathf.Sin(theta*rad));
			obj_pos+=desp*movementSpeed*Time.fixedDeltaTime;
		} 
		if (Input.GetKey(KeyCode.S)){
			if (centered){
				centered=false;
				switchingobjective=false;
				ObjectiveDesired=obj_pos;
			}
			Vector3 desp=new Vector3 (Mathf.Cos(theta*rad),0,Mathf.Sin(theta*rad));
			obj_pos+=desp*movementSpeed*Time.fixedDeltaTime;
		} 
		if (centered)ObjectiveDesired=center.transform.position;
		if (switchingobjective){
			Vector3 error=ObjectiveDesired-obj_pos;
			obj_pos=Vector3.Lerp(obj_pos, ObjectiveDesired, objectiveSwitchSpeed*Time.fixedDeltaTime/(Mathf.Pow (error.magnitude,0.5f)));
			if (error.magnitude<0.02)switchingobjective=false;
		}
		else{
			if (centered)obj_pos=ObjectiveDesired;
		}
	}
	void update_angles (){
		desiredDistance = Mathf.Clamp(desiredDistance - Input.GetAxis("Mouse ScrollWheel")*zoomspeed*Time.fixedDeltaTime, distanceMin, distanceMax);
		distance=Mathf.Lerp (distance,desiredDistance,10*Time.fixedDeltaTime);
		size -= zoomspeed*Time.fixedDeltaTime*Input.GetAxis("Mouse ScrollWheel");
		size = Mathf.Clamp(size,1f,100f);
		Camera.main.orthographicSize=size;

		if (!onlyZoom) {
						if (Input.GetKeyDown (KeyCode.Mouse1))
								LastMouse = Input.mousePosition;
						if (Input.GetKey (KeyCode.Mouse1)) {
								Vector2 dist = Input.mousePosition;
								dist -= LastMouse;
								theta -= dist.x * dragSpeed;
								phi += dist.y * dragSpeed;
								LastMouse = Input.mousePosition;
						}
						theta = theta % 360;
						phi = Mathf.Clamp (phi, 0.05f, 179.95f);
				}
	}
	void update_transform (){
		Vector3 despl = new Vector3(Mathf.Sin(phi*rad)*Mathf.Cos(theta*rad), 
		                            Mathf.Cos(phi*rad),
		                            Mathf.Sin(phi*rad)*Mathf.Sin(theta*rad));
		despl*=distance;
		transform.position = obj_pos + despl;
		transform.rotation = Quaternion.LookRotation (-despl);
	}

}
