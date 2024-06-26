
Physics Constraints (bge.constraints)
=====================================

Bullet Physics provides collision detection
and rigid body dynamics for the Blender Game Engine.

Features:

- Vehicle simulation.
- Rigid body constraints: hinge and point to point (ball socket).
- Access to internal physics settings,
  like deactivation time, and debugging features

.. module:: bge.constraints

.. note:: Note about parameter settings

   Since this API is not well documented, it can be unclear what kind of values to use for setting parameters.
   In general, damping settings should be in the range of 0 to 1 and
   stiffness settings should not be much higher than about 10.


Examples
--------

.. seealso::

   For more examples of Bullet physics and how to use them
   see the `pybullet forum <https://pybullet.org/Bullet/phpBB3/viewforum.php?f=17>`__.

.. include:: ../examples/bge.constraints.py
   :start-line: 1
   :end-line: 4

.. literalinclude:: ../examples/bge.constraints.py
   :lines: 6-


Functions
---------

.. function:: createConstraint( \
      physicsid_1, physicsid_2, constraint_type, \
      pivot_x=0.0, pivot_y=0.0, pivot_z=0.0, \
      axis_x=0.0, axis_y=0.0, axis_z=0.0, flag=0)

   Creates a constraint.

   :arg physicsid_1: The physics id of the first object in constraint.
   :type physicsid_1: int

   :arg physicsid_2: The physics id of the second object in constraint.
   :type physicsid_2: int

   :arg constraint_type: The type of the constraint, see `Create Constraint Constants`_.

   :type constraint_type: int

   :arg pivot_x: Pivot X position. (optional)
   :type pivot_x: float

   :arg pivot_y: Pivot Y position. (optional)
   :type pivot_y: float

   :arg pivot_z: Pivot Z position. (optional)
   :type pivot_z: float

   :arg axis_x: X axis angle in degrees. (optional)
   :type axis_x: float

   :arg axis_y: Y axis angle in degrees. (optional)
   :type axis_y: float

   :arg axis_z: Z axis angle in degrees. (optional)
   :type axis_z: float

   :arg flag: 128 to disable collision between linked bodies. (optional)
   :type flag: int

   :return: A constraint wrapper.
   :rtype: :class:`~bge.types.KX_ConstraintWrapper`

.. function:: createVehicle(physicsid)

   Creates a vehicle constraint.

   :arg physicsid: The physics id of the chassis object in constraint.
   :type physicsid: int

   :return: A vehicle constraint wrapper.
   :rtype: :class:`~bge.types.KX_VehicleWrapper`

.. function:: exportBulletFile(filename)

   Exports a file representing the dynamics world (usually using ``.bullet`` extension).

   See `Bullet binary serialization <http://bulletphysics.org/mediawiki-1.5.8/index.php/Bullet_binary_serialization>`__.

   :arg filename: File path.
   :type filename: str

.. function:: getAppliedImpulse(constraintId)

   :arg constraintId: The id of the constraint.
   :type constraintId: int

   :return: The most recent applied impulse.
   :rtype: float

.. function:: getVehicleConstraint(constraintId)

   :arg constraintId: The id of the vehicle constraint.
   :type constraintId: int

   :return: A vehicle constraint object.
   :rtype: :class:`~bge.types.KX_VehicleWrapper`

.. function:: getCharacter(gameobj)

   :arg gameobj: The game object with the character physics.
   :type gameobj: :class:`~bge.types.KX_GameObject`

   :return: Character wrapper.
   :rtype: :class:`~bge.types.KX_CharacterWrapper`

.. function:: removeConstraint(constraintId)

   Removes a constraint.

   :arg constraintId: The id of the constraint to be removed.
   :type constraintId: int

.. function:: setContactBreakingTreshold(breakingTreshold)

   .. note::
      Reasonable default is 0.02 (if units are meters)

   Sets tresholds to do with contact point management.

   :arg breakingTreshold: The new contact breaking treshold.
   :type breakingTreshold: float

.. function:: setDeactivationAngularTreshold(angularTreshold)

   Sets the angular velocity treshold.

   :arg angularTreshold: New deactivation angular treshold.
   :type angularTreshold: float

.. function:: setDeactivationLinearTreshold(linearTreshold)

   Sets the linear velocity treshold.

   :arg linearTreshold: New deactivation linear treshold.
   :type linearTreshold: float

.. function:: setDeactivationTime(time)

   Sets the time after which a resting rigidbody gets deactived.

   :arg time: The deactivation time.
   :type time: float

.. function:: setERPNonContact(erp)

   Sets the Error Reduction Parameter (ERP) for non-contact constraints.
   The Error Reduction Parameter (ERP) specifies what proportion of the joint error will be fixed during the next simulation step.
   If ERP = 0.0 then no correcting force is applied and the bodies will eventually drift apart as the simulation proceeds.
   If ERP = 1.0 then the simulation will attempt to fix all joint error during the next time step.
   However, setting ERP = 1.0 is not recommended, as the joint error will not be completely fixed due to various internal approximations.
   A value of ERP = 0.1 to 0.8 is recommended.

   :arg erp: The ERP parameter for non-contact constraints.
   :type erp: float [0.0, 1.0]

.. function:: setERPContact(erp2)

   Sets the Error Reduction Parameter (ERP) for contact constraints.
   The Error Reduction Parameter (ERP) specifies what proportion of the joint error will be fixed during the next simulation step.
   If ERP = 0.0 then no correcting force is applied and the bodies will eventually drift apart as the simulation proceeds.
   If ERP = 1.0 then the simulation will attempt to fix all joint error during the next time step.
   However, setting ERP = 1.0 is not recommended, as the joint error will not be completely fixed due to various internal approximations.
   A value of ERP = 0.1 to 0.8 is recommended.

   :arg erp2: The ERP parameter for contact constraints.
   :type erp2: float [0.0, 1.0]

.. function:: setCFM(cfm)

   Sets the Constraint Force Mixing (CFM) for soft constraints.
   If the Constraint Force Mixing (CFM) is set to zero, the constraint will be hard.
   If CFM is set to a positive value, it will be possible to violate the constraint by pushing on it (for example, for contact constraints by forcing the two contacting objects together).
   In other words the constraint will be soft, and the softness will increase as CFM increases.

   :arg cfm: The CFM parameter for soft constraints.
   :type cfm: float [0.0, 10000.0]

.. function:: setDebugMode(mode)

   Sets the debug mode.

   :arg mode: The new debug mode, see `Debug Mode Constants`_.

   :type mode: int

.. function:: setGravity(x, y, z)

   Sets the gravity force.

   :arg x: Gravity X force.
   :type x: float

   :arg y: Gravity Y force.
   :type y: float

   :arg z: Gravity Z force.
   :type z: float

   Sets the linear air damping for rigidbodies.

.. function:: setNumIterations(numiter)

   Sets the number of iterations for an iterative constraint solver.

   :arg numiter: New number of iterations.
   :type numiter: int

.. function:: setNumTimeSubSteps(numsubstep)

   Sets the number of substeps for each physics proceed. Tradeoff quality for performance.

   :arg numsubstep: New number of substeps.
   :type numsubstep: int

.. function:: setSolverDamping(damping)

   .. note::
      Very experimental, not recommended

   Sets the damper constant of a penalty based solver.

   :arg damping: New damping for the solver.
   :type damping: float

.. function:: setSolverTau(tau)

   .. note::
      Very experimental, not recommended

   Sets the spring constant of a penalty based solver.

   :arg tau: New tau for the solver.
   :type tau: float

.. function:: setSolverType(solverType)

   .. note::
      Very experimental, not recommended

   Sets the solver type.

   :arg solverType: The new type of the solver.
   :type solverType: int

.. function:: setSorConstant(sor)

   .. note::
      Very experimental, not recommended

   Sets the successive overrelaxation constant.

   :arg sor: New sor value.
   :type sor: float


Constants
+++++++++

.. attribute:: error

   Symbolic constant string that indicates error.

   :type: str


Debug Mode Constants
^^^^^^^^^^^^^^^^^^^^

Debug mode to be used with :func:`setDebugMode`.


.. data:: DBG_NODEBUG

   No debug.

   :type: integer

.. data:: DBG_DRAWWIREFRAME

   Draw wireframe in debug.

   :type: integer

.. data:: DBG_DRAWAABB

   Draw Axis Aligned Bounding Box in debug.

   :type: integer

.. data:: DBG_DRAWFREATURESTEXT

   Draw features text in debug.

   :type: integer

.. data:: DBG_DRAWCONTACTPOINTS

   Draw contact points in debug.

   :type: integer

.. data:: DBG_NOHELPTEXT

   Debug without help text.

   :type: integer

.. data:: DBG_DRAWTEXT

   Draw text in debug.

   :type: integer

.. data:: DBG_PROFILETIMINGS

   Draw profile timings in debug.

   :type: integer

.. data:: DBG_ENABLESATCOMPARISION

   Enable sat comparison in debug.

   :type: integer

.. data:: DBG_DISABLEBULLETLCP

   Disable Bullet LCP.

   :type: integer

.. data:: DBG_ENABLECCD

   Enable Continuous Collision Detection in debug.

   :type: integer

.. data:: DBG_DRAWCONSTRAINTS

   Draw constraints in debug.

   :type: integer

.. data:: DBG_DRAWCONSTRAINTLIMITS

   Draw constraint limits in debug.

   :type: integer

.. data:: DBG_FASTWIREFRAME

   Draw a fast wireframe in debug.

   :type: integer


Create Constraint Constants
^^^^^^^^^^^^^^^^^^^^^^^^^^^

Constraint type to be used with :func:`createConstraint`.


.. data:: POINTTOPOINT_CONSTRAINT

   .. to do

   :type: integer

.. data:: LINEHINGE_CONSTRAINT

   .. to do

   :type: integer

.. data:: ANGULAR_CONSTRAINT

   .. to do

   :type: integer

.. data:: CONETWIST_CONSTRAINT

   .. to do

   :type: integer

.. data:: VEHICLE_CONSTRAINT

   .. to do

   :type: integer

.. data:: GENERIC_6DOF_CONSTRAINT

   .. to do

   :type: integer

