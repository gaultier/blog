# Body of work

I am here recounting chronologically what I have achieved in my career until now. I have a subpar memory so it's great for me to look back on that, and may it also hopefully serve as an interesting insight for recruiters and the like of the kind of work I enjoy and am good at.

##  Crédit Mutuel (Bank), Strasbourg, France; 2013

This was my first professional experience (a 8 weeks internship) and happened at one of the big banks in the country. Like most banks that have been founded in the 20th century, they have millions and millions of lines of code in COBOL, and realised this is not tenable and need to migrate to a modern tech stack to be able to keep it simply running. 

I migrated a business application used internally. The original application was a terminal (as in: made for a *hardware* terminal), with a basic UI, talking to a DB2 database, and running on IBM's z/OS on a (real) mainframe in the confines of the bank. The final application was a C# web application, still talking to the same database, but this time running on a Windows server.

In retrospect, that was such a unique experience to work a a 30+ year old codebase running on a tech stack, OS and hardware that most developers will never encounter.

The team insisted (although not unanimously) that I write *new* COBOL code for this brand new applications for the layer that talks to the database. I think they were worried that C# could not do this job properly, for some reason? So I got to do that, in a COBOL IDE made by IBM which only COBOL developers know of. Todo list item ticked, I guess. 

All in all, that was a very interesting social experience and a great insight on how business and developers think and (try to) evolve, and how to attempt to change the tech stack of an existing running application, which is a challenge any company wil face at a moment or another. And how we engineers have a professional duty to keep learning and adapting to this changing world.


## CRNS Intern Software Engineer experimenting with the Oculus Rift (VR) CNRS, Strasbourg, France; 2014

My second internship (10 weeks), and perhaps the project I loved the most. This took place at an astronomy lab, I had the incredible privilege to have my office in the old library that was probably a few centuries old, filled with old books; the building was this 19th century observatory with a big park with beehouses... This will never be topped.

The work was also such a blast: I got to experiment with the first version of the Oculus Rift, and tinker with it. My advisor and I decided to work on two different projects: A (from scratch) 3D visualisation of planets inside the Oculus Rift for kids to 'fly' through the solar system and hopefully spark in them an interest in space exploration and astronomy. The second project was to add to an existing and large 3D simulation of planets a VR mode.

It was an exciting time, VR was all the rage and everything had to be figured out: the motion sickness, the controller (it turns out that most people are not so good at using a keyboard and mouse while being completely blind and a video game console controller is much more intuitive), how to plug the Oculus Rift SDK to an existing codebase, the performance, etc.

Even though I wished I had a tad more time, I delivered both. Since we foresaw we would not have time for everything, the solar system visualisation got cut down to blue sparkling cubes in 3D space, but the Oculus Rift worked beautifully with it. The second project also worked, although we had visual artifacts in some cases when traveling far distances, which I suspected was due to floating point precision issues.

Performance was initially also a challenge since VR consists of rendering the same scene twice, once for each eye, with a slight change in where the camera is in 3D space (since the camera is your eye, in a way). And 3D rendering will always be a domain where performance is paramount. It's interesting to note that new 3D APIs such as Vulkan do offer features for VR in the form of extensions to speed that up possibly in hardware, having the GPU do the heavy lifting. But back in 2014, there was nothing like that. Also, 3D APIs have really evolved in the last decade, becoming more low level and giving the developer more control, power, but also responsabilities.

My major performance stepstone was moving from rendering everything in the scene to using an Octree to only render entities in the 'zone' where the camera is, or is looking at.

I used OpenGL and C++ for the first project, and C for the second one since the existing codebase was in C.

3D, VR, extending an existing codebase, starting a new project from scratch with 'carte blanche': I learned a ton! And my advisor, fellow coworkers exploring this space (notably trying to do the same with a different VR headset, the Sony Morpheus), and I even got to publish a paper based on our work, that got submitted: [Immersive-3D visualization of astronomical data](cnrs.pdf), [link](https://arxiv.org/abs/1607.08874).
